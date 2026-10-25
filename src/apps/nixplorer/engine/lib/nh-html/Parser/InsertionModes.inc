/* Additional insertion modes within the original tree constructor. */
static const char *current_name(nh_html_Parser *p) {
    nh_webidl_Object *e=NH_WEBIDL_GET_DOM_ELEMENT(nh_html_getCurrentNode(p));
    return e?nh_dom_getLocalName(e)->p:"";
}
static void reset_mode(nh_html_Parser *p) {
    for(unsigned long i=p->OpenElements.size;i;i--) {
        nh_webidl_Object *e=NH_WEBIDL_GET_DOM_ELEMENT(p->OpenElements.pp[i-1]);
        const char *n=e?nh_dom_getLocalName(e)->p:"";
        if(!strcmp(n,"select")){p->insertionMode=NH_HTML_INSERTION_MODE_IN_SELECT;return;}
        if(!strcmp(n,"td")||!strcmp(n,"th")){p->insertionMode=NH_HTML_INSERTION_MODE_IN_CELL;return;}
        if(!strcmp(n,"tr")){p->insertionMode=NH_HTML_INSERTION_MODE_IN_ROW;return;}
        if(!strcmp(n,"tbody")||!strcmp(n,"thead")||!strcmp(n,"tfoot")){p->insertionMode=NH_HTML_INSERTION_MODE_IN_TABLE_BODY;return;}
        if(!strcmp(n,"caption")){p->insertionMode=NH_HTML_INSERTION_MODE_IN_CAPTION;return;}
        if(!strcmp(n,"colgroup")){p->insertionMode=NH_HTML_INSERTION_MODE_IN_COLUMN_GROUP;return;}
        if(!strcmp(n,"table")){p->insertionMode=NH_HTML_INSERTION_MODE_IN_TABLE;return;}
        if(!strcmp(n,"template")){p->insertionMode=NH_HTML_INSERTION_MODE_IN_TEMPLATE;return;}
        if(!strcmp(n,"head")){p->insertionMode=NH_HTML_INSERTION_MODE_IN_HEAD;return;}
        if(!strcmp(n,"body"))break;
    }
    p->insertionMode=NH_HTML_INSERTION_MODE_IN_BODY;
}
static void clear_formatting_marker(nh_html_Parser *p) {
    while(p->ActiveFormattingElements.size)if(!nh_core_popStack(&p->ActiveFormattingElements))break;
}
static void pop_through(nh_html_Parser *p,const char *name) {
    while(p->OpenElements.size) {
        bool found=!strcmp(current_name(p),name);
        nh_html_popCurrentNode(p);
        if(found)break;
    }
}
static NH_API_RESULT close_template(nh_html_Parser *p) {
    if(!nh_html_hasElementInScope(p,"template"))return NH_API_SUCCESS;
    NH_CORE_CHECK(nh_html_generateAllImpliedEndTags(p))
    pop_through(p,"template");clear_formatting_marker(p);
    nh_core_popStack(&p->TemplateInsertionModes);reset_mode(p);
    return NH_API_SUCCESS;
}
static NH_API_RESULT implied_element(nh_html_Parser *p,NH_HTML_TAG tag) {
    return nh_html_insertHTMLElement(p,nh_html_getEmptyStartTagToken(tag))?NH_API_SUCCESS:NH_API_ERROR_MEMORY_ALLOCATION;
}
static void close_cell(nh_html_Parser *p) {
    const char *name=nh_html_hasElementInTableScope(p,"td")?"td":"th";
    pop_through(p,name);clear_formatting_marker(p);p->insertionMode=NH_HTML_INSERTION_MODE_IN_ROW;
}
static NH_API_RESULT nh_html_processSelect(nh_html_Parser *p) {
    nh_html_Token *t=p->Token_p;int tag=t->StartOrEndTag.tag;
    if(t->type==NH_HTML_TOKEN_CHARACTER)return nh_html_insertCharacter(p,NULL,NULL);
    if(t->type==NH_HTML_TOKEN_COMMENT)return nh_html_insertComment(p,NULL);
    if(t->type==NH_HTML_TOKEN_END_OF_FILE)return nh_html_processInBody(p);
    if(t->type==NH_HTML_TOKEN_START_TAG) {
        if(tag==NH_HTML_TAG_OPTION||tag==NH_HTML_TAG_OPTGROUP) {
            if(!strcmp(current_name(p),"option"))nh_html_popCurrentNode(p);
            if(tag==NH_HTML_TAG_OPTGROUP&&!strcmp(current_name(p),"optgroup"))nh_html_popCurrentNode(p);
            return nh_html_insertHTMLElement(p,t)?NH_API_SUCCESS:NH_API_ERROR_MEMORY_ALLOCATION;
        }
        if(tag==NH_HTML_TAG_SELECT||tag==NH_HTML_TAG_INPUT||tag==NH_HTML_TAG_TEXTAREA||tag==NH_HTML_TAG_KEYGEN) {
            if(!nh_html_hasElementInSelectScope(p,"select"))return NH_API_SUCCESS;
            pop_through(p,"select");reset_mode(p);
            return tag==NH_HTML_TAG_SELECT?NH_API_SUCCESS:nh_html_reprocessToken(p,p->insertionMode);
        }
        if(tag==NH_HTML_TAG_SCRIPT||tag==NH_HTML_TAG_TEMPLATE)return nh_html_processInHead(p);
    }
    if(t->type==NH_HTML_TOKEN_END_TAG) {
        if(tag==NH_HTML_TAG_OPTION&&!strcmp(current_name(p),"option"))nh_html_popCurrentNode(p);
        else if(tag==NH_HTML_TAG_OPTGROUP) {
            if(!strcmp(current_name(p),"option")&&p->OpenElements.size>1) {
                nh_webidl_Object *e=NH_WEBIDL_GET_DOM_ELEMENT(p->OpenElements.pp[p->OpenElements.size-2]);
                if(e&&!strcmp(nh_dom_getLocalName(e)->p,"optgroup"))nh_html_popCurrentNode(p);
            }
            if(!strcmp(current_name(p),"optgroup"))nh_html_popCurrentNode(p);
        } else if(tag==NH_HTML_TAG_SELECT&&nh_html_hasElementInSelectScope(p,"select")) {
            pop_through(p,"select");reset_mode(p);
        }
    }
    return NH_API_SUCCESS;
}
static bool table_boundary(int tag) {
    return tag==NH_HTML_TAG_TABLE||tag==NH_HTML_TAG_TBODY||tag==NH_HTML_TAG_THEAD||tag==NH_HTML_TAG_TFOOT||tag==NH_HTML_TAG_TR;
}
static NH_API_RESULT nh_html_processTable(nh_html_Parser *p) {
    nh_html_Token *t=p->Token_p;int tag=t->StartOrEndTag.tag;
    bool start=t->type==NH_HTML_TOKEN_START_TAG,end=t->type==NH_HTML_TOKEN_END_TAG;
    NH_HTML_INSERTION_MODE mode=p->insertionMode;
    if(mode==NH_HTML_INSERTION_MODE_IN_CELL) {
        if((end&&(tag==NH_HTML_TAG_TD||tag==NH_HTML_TAG_TH))||((start||end)&&(table_boundary(tag)||(start&&(tag==NH_HTML_TAG_TD||tag==NH_HTML_TAG_TH))))) {
            if(!nh_html_hasElementInTableScope(p,"td")&&!nh_html_hasElementInTableScope(p,"th"))return NH_API_SUCCESS;
            close_cell(p);
            return end&&(tag==NH_HTML_TAG_TD||tag==NH_HTML_TAG_TH)?NH_API_SUCCESS:nh_html_reprocessToken(p,p->insertionMode);
        }
        return nh_html_processInBody(p);
    }
    if(mode==NH_HTML_INSERTION_MODE_IN_CAPTION) {
        if((end&&tag==NH_HTML_TAG_CAPTION)||((start||end)&&table_boundary(tag))) {
            if(!nh_html_hasElementInTableScope(p,"caption"))return NH_API_SUCCESS;
            pop_through(p,"caption");clear_formatting_marker(p);p->insertionMode=NH_HTML_INSERTION_MODE_IN_TABLE;
            return end&&tag==NH_HTML_TAG_CAPTION?NH_API_SUCCESS:nh_html_reprocessToken(p,p->insertionMode);
        }
        return nh_html_processInBody(p);
    }
    if(mode==NH_HTML_INSERTION_MODE_IN_COLUMN_GROUP) {
        if(start&&tag==NH_HTML_TAG_COL) {
            NH_CORE_CHECK_NULL(nh_html_insertHTMLElement(p,t))
            nh_html_popCurrentNode(p);return NH_API_SUCCESS;
        }
        if(t->type==NH_HTML_TOKEN_CHARACTER&&t->CommentOrCharacter.Data.length==1&&t->CommentOrCharacter.Data.p[0]==' ')return nh_html_insertCharacter(p,NULL,NULL);
        if(!strcmp(current_name(p),"colgroup")) {nh_html_popCurrentNode(p);p->insertionMode=NH_HTML_INSERTION_MODE_IN_TABLE;}
        else return NH_API_SUCCESS;
        return end&&tag==NH_HTML_TAG_COLGROUP?NH_API_SUCCESS:nh_html_reprocessToken(p,p->insertionMode);
    }
    if(mode==NH_HTML_INSERTION_MODE_IN_ROW) {
        if(start&&(tag==NH_HTML_TAG_TD||tag==NH_HTML_TAG_TH)) {
            while(strcmp(current_name(p),"tr")&&strcmp(current_name(p),"template")&&p->OpenElements.size>1)nh_html_popCurrentNode(p);
            NH_CORE_CHECK_NULL(nh_html_insertHTMLElement(p,t))
            NH_CORE_CHECK(nh_html_insertMarker(p))
            p->insertionMode=NH_HTML_INSERTION_MODE_IN_CELL;return NH_API_SUCCESS;
        }
        if((end&&table_boundary(tag))||(start&&table_boundary(tag))) {
            if(!nh_html_hasElementInTableScope(p,"tr"))return NH_API_SUCCESS;
            pop_through(p,"tr");p->insertionMode=NH_HTML_INSERTION_MODE_IN_TABLE_BODY;
            return end&&tag==NH_HTML_TAG_TR?NH_API_SUCCESS:nh_html_reprocessToken(p,p->insertionMode);
        }
    }
    if(mode==NH_HTML_INSERTION_MODE_IN_TABLE_BODY) {
        if(start&&(tag==NH_HTML_TAG_TD||tag==NH_HTML_TAG_TH||tag==NH_HTML_TAG_TR)) {
            if(tag!=NH_HTML_TAG_TR) {NH_CORE_CHECK(implied_element(p,NH_HTML_TAG_TR))}
            else {NH_CORE_CHECK_NULL(nh_html_insertHTMLElement(p,t))}
            p->insertionMode=NH_HTML_INSERTION_MODE_IN_ROW;
            return tag==NH_HTML_TAG_TR?NH_API_SUCCESS:nh_html_reprocessToken(p,p->insertionMode);
        }
        if((end&&table_boundary(tag))||(start&&(tag==NH_HTML_TAG_TBODY||tag==NH_HTML_TAG_THEAD||tag==NH_HTML_TAG_TFOOT))) {
            while(strcmp(current_name(p),"tbody")&&strcmp(current_name(p),"thead")&&strcmp(current_name(p),"tfoot")&&p->OpenElements.size>1)nh_html_popCurrentNode(p);
            bool matches=(tag==NH_HTML_TAG_TBODY&&!strcmp(current_name(p),"tbody"))||(tag==NH_HTML_TAG_THEAD&&!strcmp(current_name(p),"thead"))||(tag==NH_HTML_TAG_TFOOT&&!strcmp(current_name(p),"tfoot"));
            nh_html_popCurrentNode(p);p->insertionMode=NH_HTML_INSERTION_MODE_IN_TABLE;
            return end&&matches?NH_API_SUCCESS:nh_html_reprocessToken(p,p->insertionMode);
        }
    }
    if(start) {
        if(tag==NH_HTML_TAG_CAPTION||tag==NH_HTML_TAG_COLGROUP||tag==NH_HTML_TAG_TBODY||tag==NH_HTML_TAG_THEAD||tag==NH_HTML_TAG_TFOOT) {
            while(strcmp(current_name(p),"table")&&strcmp(current_name(p),"template")&&p->OpenElements.size>1)nh_html_popCurrentNode(p);
            NH_CORE_CHECK_NULL(nh_html_insertHTMLElement(p,t))
            if(tag==NH_HTML_TAG_CAPTION){NH_CORE_CHECK(nh_html_insertMarker(p))}
            p->insertionMode=tag==NH_HTML_TAG_CAPTION?NH_HTML_INSERTION_MODE_IN_CAPTION:tag==NH_HTML_TAG_COLGROUP?NH_HTML_INSERTION_MODE_IN_COLUMN_GROUP:NH_HTML_INSERTION_MODE_IN_TABLE_BODY;
            return NH_API_SUCCESS;
        }
        if(tag==NH_HTML_TAG_COL) {NH_CORE_CHECK(implied_element(p,NH_HTML_TAG_COLGROUP)) return nh_html_reprocessToken(p,NH_HTML_INSERTION_MODE_IN_COLUMN_GROUP);}
        if(tag==NH_HTML_TAG_TR||tag==NH_HTML_TAG_TD||tag==NH_HTML_TAG_TH) {NH_CORE_CHECK(implied_element(p,NH_HTML_TAG_TBODY)) return nh_html_reprocessToken(p,NH_HTML_INSERTION_MODE_IN_TABLE_BODY);}
        if(tag==NH_HTML_TAG_SCRIPT||tag==NH_HTML_TAG_STYLE||tag==NH_HTML_TAG_TEMPLATE)return nh_html_processInHead(p);
    }
    if((start||end)&&tag==NH_HTML_TAG_TABLE) {
        if(!nh_html_hasElementInTableScope(p,"table"))return NH_API_SUCCESS;
        pop_through(p,"table");reset_mode(p);
        return end?NH_API_SUCCESS:nh_html_reprocessToken(p,p->insertionMode);
    }
    if(t->type==NH_HTML_TOKEN_COMMENT)return nh_html_insertComment(p,NULL);
    if(t->type==NH_HTML_TOKEN_END_OF_FILE)return nh_html_processInBody(p);
    if(t->type==NH_HTML_TOKEN_CHARACTER) {
        bool whitespace=true;
        for(unsigned long i=0;i<t->CommentOrCharacter.Data.length;i++) {
            unsigned char c=t->CommentOrCharacter.Data.p[i];
            if(c!=' '&&c!='\t'&&c!='\r'&&c!='\n'&&c!='\f')whitespace=false;
        }
        if(whitespace)return nh_html_insertCharacter(p,NULL,NULL);
    }
    p->fosterParenting=true;
    NH_API_RESULT result=nh_html_processInBody(p);
    p->fosterParenting=false;
    return result;
}
static NH_API_RESULT nh_html_processTemplate(nh_html_Parser *p) {
    if(p->Token_p->type==NH_HTML_TOKEN_END_OF_FILE) {
        if(nh_html_hasElementInScope(p,"template")) {NH_CORE_CHECK(close_template(p)) return nh_html_reprocessToken(p,p->insertionMode);}
        return nh_html_processInBody(p);
    }
    if(p->Token_p->type==NH_HTML_TOKEN_START_TAG) {
        int tag=p->Token_p->StartOrEndTag.tag;
        if(tag==NH_HTML_TAG_STYLE||tag==NH_HTML_TAG_SCRIPT||tag==NH_HTML_TAG_TEMPLATE||tag==NH_HTML_TAG_META||tag==NH_HTML_TAG_LINK)return nh_html_processInHead(p);
        if(tag==NH_HTML_TAG_TR)return nh_html_reprocessToken(p,NH_HTML_INSERTION_MODE_IN_TABLE_BODY);
        if(tag==NH_HTML_TAG_TD||tag==NH_HTML_TAG_TH)return nh_html_reprocessToken(p,NH_HTML_INSERTION_MODE_IN_ROW);
        if(tag==NH_HTML_TAG_TBODY||tag==NH_HTML_TAG_THEAD||tag==NH_HTML_TAG_TFOOT||tag==NH_HTML_TAG_CAPTION||tag==NH_HTML_TAG_COLGROUP)return nh_html_reprocessToken(p,NH_HTML_INSERTION_MODE_IN_TABLE);
    }
    if(p->Token_p->type==NH_HTML_TOKEN_END_TAG)return NH_API_SUCCESS;
    return nh_html_reprocessToken(p,NH_HTML_INSERTION_MODE_IN_BODY);
}
