// LICENSE NOTICE ==================================================================================

/**
 * Netzhaut - Web Browser Engine
 * Copyright (C) 2022  Dajo Frey
 * Published under GNU LGPL. See Netzhaut/LICENSE.LGPL file.
 */

// FILE DESCRIPTION ================================================================================

/**
 * https://www.w3.org/TR/selectors-4/
 */

// INCLUDES ========================================================================================

#include "Match.h"
#include "../../nh-dom/Interfaces/DOM.h"

#include "../Common/Log.h"

#include "../../nh-core/System/Memory.h"
#include "../../nh-core/Util/List.h"

#include "../../nh-dom/Interfaces/Element.h"
#include "../../nh-dom/Interfaces/Attr.h"

#include "../../nh-encoding/Encodings/UTF8.h"

#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
	
// FUNCTIONS =======================================================================================

static bool nh_css_matchTypeSelector(
    nh_css_SelectorParseNode *TypeSelector_p, nh_webidl_Object *DOMElement_p)
{
    bool match = false;

    if (((nh_css_SelectorParseNode*)TypeSelector_p->Children.pp[0])->type == NH_CSS_SELECTOR_PARSE_NODE_WQ_NAME) 
    {
        nh_css_SelectorParseNode *WqName_p = TypeSelector_p->Children.pp[0];

        if (WqName_p->Children.size == 1) 
        {
            nh_css_Token *IdentTok_p = ((nh_css_SelectorParseNode*)WqName_p->Children.pp[0])->Token_p;
            nh_encoding_UTF8String Identifier = nh_encoding_encodeUTF8(IdentTok_p->Other.Value.p, IdentTok_p->Other.Value.length);
            nh_encoding_UTF8String *LocalName_p = nh_dom_getLocalName(DOMElement_p);
            if (!strcmp(LocalName_p->p, Identifier.p)) {match = true;}
            nh_encoding_freeUTF8(&Identifier);
        }
    }
 
    if (((nh_css_SelectorParseNode*)TypeSelector_p->Children.pp[0])->type == NH_CSS_SELECTOR_PARSE_NODE_NS_PREFIX) 
    {
        // TODO
    }

    if (((nh_css_SelectorParseNode*)TypeSelector_p->Children.pp[0])->Token_p)
    {
        nh_css_Token *Token_p = ((nh_css_SelectorParseNode*)TypeSelector_p->Children.pp[0])->Token_p;
        if (Token_p->type == NH_CSS_TOKEN_DELIM && Token_p->Delim.value == 0x2A) {
            // universal selector (*)
           match = true;
        } 
    }

    return match;
}

static bool nh_css_matchIdSelector(
    nh_css_SelectorParseNode *IdSelector_p, nh_webidl_Object *DOMElement_p)
{
    bool match = false;

    nh_css_Token *HashTok_p = ((nh_css_SelectorParseNode*)IdSelector_p->Children.pp[0])->Token_p;
    nh_encoding_UTF8String Identifier = nh_encoding_encodeUTF8(HashTok_p->Hash.Value.p, HashTok_p->Hash.Value.length);

    nh_webidl_Object *Attr_p = nh_dom_getAttrByLocalName(DOMElement_p, "id");
    if (Attr_p) {
        nh_encoding_UTF8String *Value_p = nh_dom_getAttrValue(Attr_p);
        if (!strcmp(Value_p->p, Identifier.p)) {
            match = true;
        }
    }

    nh_encoding_freeUTF8(&Identifier);
 
    return match;
}

static bool nh_css_matchClassSelector(
    nh_css_SelectorParseNode *ClassSelector_p, nh_webidl_Object *DOMElement_p)
{
    bool match = false;

    nh_css_Token *IdentTok_p = ((nh_css_SelectorParseNode*)ClassSelector_p->Children.pp[1])->Token_p;
    nh_encoding_UTF8String Identifier = nh_encoding_encodeUTF8(IdentTok_p->Other.Value.p, IdentTok_p->Other.Value.length);

    match = nh_dom_classListContains(DOMElement_p,Identifier.p);

    nh_encoding_freeUTF8(&Identifier);
 
    return match;
}

static nh_webidl_Object *parent_element(nh_webidl_Object *o) {
    return nh_dom_getParentElement(NH_WEBIDL_GET_DOM_NODE(o));
}
static nh_webidl_Object *sibling_element(nh_webidl_Object *o,bool next) {
    while((o=next?nh_dom_getNextSibling(o):nh_dom_getPreviousSibling(o)))if(NH_WEBIDL_GET_DOM_ELEMENT(o))return o;
    return NULL;
}
static bool match_pseudo(nh_css_SelectorParseNode *selector,nh_webidl_Object *o) {
    if(selector->Children.size<2)return false;
    nh_css_Token *token=((nh_css_SelectorParseNode *)selector->Children.pp[1])->Token_p;
    if(!token||token->type!=NH_CSS_TOKEN_IDENT)return false;
    nh_encoding_UTF8String name=nh_encoding_encodeUTF8(token->Other.Value.p,token->Other.Value.length);
    bool match=false;
    if(!strcmp(name.p,"root"))match=nh_dom_getNodeType(nh_dom_getParentNode(NH_WEBIDL_GET_DOM_NODE(o)))==9;
    else if(!strcmp(name.p,"first-child"))match=!sibling_element(o,false);
    else if(!strcmp(name.p,"last-child"))match=!sibling_element(o,true);
    else if(!strcmp(name.p,"only-child"))match=!sibling_element(o,false)&&!sibling_element(o,true);
    else if(!strcmp(name.p,"empty")) {
        match=true;nh_core_List *c=nh_dom_getNodeChildren(NH_WEBIDL_GET_DOM_NODE(o));
        for(unsigned long i=0;i<c->size;i++)if(nh_dom_getNodeType(c->pp[i])==1||((nh_dom_getNodeType(c->pp[i])==3||nh_dom_getNodeType(c->pp[i])==4)&&nh_dom_getCharacterDataLength(c->pp[i])))match=false;
    } else if(!strcmp(name.p,"link")||!strcmp(name.p,"any-link")) {
        const char *tag=nh_dom_getLocalName(NH_WEBIDL_GET_DOM_ELEMENT(o))->p;
        match=(!strcmp(tag,"a")||!strcmp(tag,"area"))&&nh_dom_getAttribute(o,"href");
    } else if(!strcmp(name.p,"disabled"))match=nh_dom_getAttribute(o,"disabled")!=NULL;
    else if(!strcmp(name.p,"checked"))match=nh_dom_getAttribute(o,"checked")||nh_dom_getAttribute(o,"selected");
    nh_encoding_freeUTF8(&name);return match;
}
static bool match_attribute(nh_css_SelectorParseNode *selector,nh_webidl_Object *o) {
    if(selector->Children.size<3)return false;
    nh_css_SelectorParseNode *name=selector->Children.pp[1];
    if(name->Children.size!=1)return false;
    nh_css_Token *token=((nh_css_SelectorParseNode *)name->Children.pp[0])->Token_p;
    nh_encoding_UTF8String key=nh_encoding_encodeUTF8(token->Other.Value.p,token->Other.Value.length);
    const nh_encoding_UTF8String *value=nh_dom_getAttribute(o,key.p);
    nh_encoding_freeUTF8(&key);
    if(!value)return false;
    if(selector->Children.size==3)return true;
    nh_css_SelectorParseNode *matcher=selector->Children.pp[2];
    token=((nh_css_SelectorParseNode *)selector->Children.pp[3])->Token_p;
    nh_encoding_UTF8String wanted=nh_encoding_encodeUTF8(token->Other.Value.p,token->Other.Value.length);
    unsigned op=((nh_css_SelectorParseNode *)matcher->Children.pp[0])->Token_p->Delim.value;
    bool match=false;size_t n=wanted.length;
    if(op=='=')match=value->length==n&&!memcmp(value->p,wanted.p,n);
    else if(n&&op=='^')match=value->length>=n&&!memcmp(value->p,wanted.p,n);
    else if(n&&op=='$')match=value->length>=n&&!memcmp(value->p+value->length-n,wanted.p,n);
    else if(n&&op=='*')match=strstr(value->p,wanted.p)!=NULL;
    else if(op=='|')match=!strcmp(value->p,wanted.p)||(value->length>n&&!memcmp(value->p,wanted.p,n)&&value->p[n]=='-');
    else if(n&&op=='~') {
        for(unsigned long i=0;i<value->length;) {
            while(i<value->length&&(value->p[i]==' '||value->p[i]=='\t'||value->p[i]=='\n'||value->p[i]=='\r'||value->p[i]=='\f'))i++;
            unsigned long start=i;
            while(i<value->length&&value->p[i]!=' '&&value->p[i]!='\t'&&value->p[i]!='\n'&&value->p[i]!='\r'&&value->p[i]!='\f')i++;
            if(i-start==n&&!memcmp(value->p+start,wanted.p,n))match=true;
        }
    }
    nh_encoding_freeUTF8(&wanted);return match;
}

static bool nh_css_matchSubclassSelector(
    nh_css_SelectorParseNode *SubclassSelector_p, nh_webidl_Object *DOMElement_p)
{
    bool match = false;

    switch (((nh_css_SelectorParseNode*)SubclassSelector_p->Children.pp[0])->type)
    {
        case NH_CSS_SELECTOR_PARSE_NODE_CLASS_SELECTOR :
            match = nh_css_matchClassSelector(SubclassSelector_p->Children.pp[0], DOMElement_p);
            break;
        case NH_CSS_SELECTOR_PARSE_NODE_ATTRIBUTE_SELECTOR :
            match = match_attribute(SubclassSelector_p->Children.pp[0], DOMElement_p);
            break;
        case NH_CSS_SELECTOR_PARSE_NODE_PSEUDO_CLASS_SELECTOR :
            match = match_pseudo(SubclassSelector_p->Children.pp[0], DOMElement_p);
            break;
        case NH_CSS_SELECTOR_PARSE_NODE_ID_SELECTOR :
            match = nh_css_matchIdSelector(SubclassSelector_p->Children.pp[0], DOMElement_p);
            break;
    }
 
    return match;
}

static bool nh_css_matchCompoundSelector(
    nh_css_SelectorParseNode *CompoundSelector_p, nh_webidl_Object *DOMElement_p)
{
    bool match = false;

    for (int i = 0; i < CompoundSelector_p->Children.size; ++i)
    {
        switch (((nh_css_SelectorParseNode*)CompoundSelector_p->Children.pp[i])->type)
        {
            case NH_CSS_SELECTOR_PARSE_NODE_TYPE_SELECTOR :
                match = nh_css_matchTypeSelector(CompoundSelector_p->Children.pp[i], DOMElement_p);
                break;
            case NH_CSS_SELECTOR_PARSE_NODE_SUBCLASS_SELECTOR :
                match = nh_css_matchSubclassSelector(CompoundSelector_p->Children.pp[i], DOMElement_p);
                break;
            case NH_CSS_SELECTOR_PARSE_NODE_PSEUDO_ELEMENT_SELECTOR :
                match=false;
                break;
            case NH_CSS_SELECTOR_PARSE_NODE_PSEUDO_CLASS_SELECTOR :
                match=match_pseudo(CompoundSelector_p->Children.pp[i],DOMElement_p);
                break;
        }

        if (!match) {break;}
    }

    return match;
}

static bool match_complex_at(nh_css_SelectorParseNode *selector,int index,nh_webidl_Object *o) {
    o=o?NH_WEBIDL_GET_DOM_ELEMENT(o):NULL;
    if(!o||index<0||!nh_css_matchCompoundSelector(selector->Children.pp[index],o))return false;
    if(!index)return true;
    int previous=index-1;unsigned op=' ';
    nh_css_SelectorParseNode *between=selector->Children.pp[previous];
    if(between->type==NH_CSS_SELECTOR_PARSE_NODE_COMBINATOR) {
        nh_css_Token *token=((nh_css_SelectorParseNode *)between->Children.pp[0])->Token_p;
        if(token->type==NH_CSS_TOKEN_DELIM)op=token->Delim.value;
        previous--;
    }
    if(op=='>')return match_complex_at(selector,previous,parent_element(o));
    if(op=='+')return match_complex_at(selector,previous,sibling_element(o,false));
    if(op=='~') {
        while((o=sibling_element(o,false)))if(match_complex_at(selector,previous,o))return true;
    } else while((o=parent_element(o)))if(match_complex_at(selector,previous,o))return true;
    return false;
}
static bool nh_css_matchComplexSelector(nh_css_SelectorParseNode *selector,nh_webidl_Object *o) {
    return selector&&selector->Children.size&&match_complex_at(selector,selector->Children.size-1,o);
}

static bool nh_css_matchComplexSelectorList(
    nh_css_SelectorParseNode *ComplexSelectorList_p, nh_webidl_Object *DOMElement_p)
{
    for (int i = 0; i < ComplexSelectorList_p->Children.size; ++i) 
    {
        nh_css_SelectorParseNode *ComplexSelector_p = ComplexSelectorList_p->Children.pp[i];
        bool match = nh_css_matchComplexSelector(ComplexSelector_p, DOMElement_p);
        if (match) {return true;}
    }

    return false;
}

bool nh_css_matchSelectors(
    nh_css_SelectorParseNode *Selectors_p, nh_webidl_Object *DOMElement_p)
{
    if (Selectors_p->type != NH_CSS_SELECTOR_PARSE_NODE_SELECTOR_LIST) {return false;}
    if (Selectors_p->Children.size != 1) {return false;}

    nh_css_SelectorParseNode *ComplexSelectorList_p = Selectors_p->Children.pp[0];

    return nh_css_matchComplexSelectorList(ComplexSelectorList_p, DOMElement_p);
}
