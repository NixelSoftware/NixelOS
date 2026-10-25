// LICENSE NOTICE ==================================================================================

/**
 * Netzhaut - Web Browser Engine
 * Copyright (C) 2022  Dajo Frey
 * Published under GNU LGPL. See Netzhaut/LICENSE.LGPL file.
 */

// INCLUDES ========================================================================================

#include "TreeConstructionHelper.h"
#include "AdoptionAgencyAlgorithm.h"
#include "../Interfaces/HTMLElement.h"
#include "Algorithms.h"

#include "../../nh-dom/Interfaces/Attr.h"
#include "../../nh-dom/Interfaces/Element.h"
#include "../../nh-dom/Interfaces/Node.h"
#include "../../nh-dom/Interfaces/NodeList.h"
#include "../../nh-dom/Interfaces/Comment.h"
#include "../../nh-dom/Interfaces/Document.h"
#include "../../nh-dom/Interfaces/DocumentType.h"
#include "../../nh-dom/Interfaces/Text.h"

#include "../../nh-core/Util/Array.h"
#include "../../nh-webidl/Runtime/Object.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// ADOPTION AGENCY ALGORITHM =======================================================================

static long position(nh_core_List *list,nh_webidl_Object *object) {
    for(unsigned long i=0;i<list->size;i++)if(list->pp[i]==object)return i;
    return -1;
}
static NH_API_RESULT any_other_end_tag(nh_html_Parser *p,const char *subject) {
    for(unsigned long i=p->OpenElements.size;i;i--) {
        nh_webidl_Object *o=p->OpenElements.pp[i-1];
        if(!strcmp(nh_dom_getLocalName(NH_WEBIDL_GET_DOM_ELEMENT(o))->p,subject)) {
            NH_CORE_CHECK(nh_html_generateImpliedEndTags(p,(char *)subject))
            while(p->OpenElements.size>=i)nh_html_popCurrentNode(p);
            return NH_API_SUCCESS;
        }
        if(nh_html_inSpecialCategory(o))break;
    }
    return NH_API_SUCCESS;
}
/* The original adoption-agency skeleton now uses the DOM's validated move
 * operations for both formatting reconstruction and misnested block content. */
NH_API_RESULT nh_html_runAdoptionAgencyAlgorithm(nh_html_Parser *p,nh_html_Token *token) {
    const char *subject=token->StartOrEndTag.TagName.p;
    nh_webidl_Object *current=nh_html_getCurrentNode(p);
    if(!strcmp(nh_dom_getLocalName(NH_WEBIDL_GET_DOM_ELEMENT(current))->p,subject)&&position(&p->ActiveFormattingElements,current)<0) {
        nh_html_popCurrentNode(p);return NH_API_SUCCESS;
    }
    for(unsigned outer=0;outer<8;outer++) {
        nh_webidl_Object *format=NULL;
        for(unsigned long i=p->ActiveFormattingElements.size;i;i--) {
            nh_webidl_Object *o=p->ActiveFormattingElements.pp[i-1];
            if(!o)break;
            if(!strcmp(nh_dom_getLocalName(NH_WEBIDL_GET_DOM_ELEMENT(o))->p,subject)){format=o;break;}
        }
        if(!format)return any_other_end_tag(p,subject);
        long fi=position(&p->OpenElements,format);
        if(fi<0){nh_core_removeFromList2(&p->ActiveFormattingElements,false,format);return NH_API_SUCCESS;}
        if(!nh_html_hasElementInScope(p,(char *)subject))return NH_API_SUCCESS;
        nh_webidl_Object *furthest=NULL;
        long furthestIndex=-1;
        for(unsigned long i=fi+1;i<p->OpenElements.size;i++)if(nh_html_inSpecialCategory(p->OpenElements.pp[i])){furthest=p->OpenElements.pp[i];furthestIndex=i;break;}
        if(!furthest) {
            while(p->OpenElements.size>(unsigned long)fi)nh_html_popCurrentNode(p);
            nh_core_removeFromList2(&p->ActiveFormattingElements,false,format);return NH_API_SUCCESS;
        }
        if(fi==0)return NH_API_ERROR_BAD_STATE;
        nh_webidl_Object *ancestor=p->OpenElements.pp[fi-1],*last=furthest;
        long bookmark=position(&p->ActiveFormattingElements,format),index=furthestIndex;
        for(unsigned inner=1;index>0;inner++) {
            nh_webidl_Object *o=p->OpenElements.pp[--index];
            if(o==format)break;
            long active=position(&p->ActiveFormattingElements,o);
            if(inner>3&&active>=0) {
                nh_core_removeFromList(&p->ActiveFormattingElements,false,active);
                if(active<bookmark)bookmark--;
                active=-1;
            }
            if(active<0){nh_core_removeFromList(&p->OpenElements,false,index);continue;}
            nh_html_Token *saved=nh_html_getToken(NH_WEBIDL_GET_HTML_ELEMENT(o));
            nh_webidl_Object *copy=nh_html_createElementForToken(saved,&NH_WEBIDL_HTML_NAMESPACE,ancestor);
            if(!copy)return NH_API_ERROR_MEMORY_ALLOCATION;
            p->ActiveFormattingElements.pp[active]=copy;p->OpenElements.pp[index]=copy;
            if(last==furthest)bookmark=active+1;
            NH_CORE_CHECK(nh_dom_appendToNode(NH_WEBIDL_GET_DOM_NODE(copy),last))
            last=copy;
        }
        nh_webidl_Object *target=ancestor;
        bool savedFoster=p->fosterParenting;p->fosterParenting=true;
        NH_WEBIDL_UNSIGNED_LONG insertion=nh_html_getAppropriatePlaceForInsertingNode(p,&target);
        p->fosterParenting=savedFoster;
        NH_CORE_CHECK(nh_dom_insertIntoNode(NH_WEBIDL_GET_DOM_NODE(target),last,insertion))
        nh_webidl_Object *copy=nh_html_createElementForToken(nh_html_getToken(NH_WEBIDL_GET_HTML_ELEMENT(format)),&NH_WEBIDL_HTML_NAMESPACE,furthest);
        if(!copy)return NH_API_ERROR_MEMORY_ALLOCATION;
        nh_core_List *children=nh_dom_getNodeChildren(NH_WEBIDL_GET_DOM_NODE(furthest));
        while(children->size){NH_CORE_CHECK(nh_dom_appendToNode(NH_WEBIDL_GET_DOM_NODE(copy),children->pp[0]))}
        NH_CORE_CHECK(nh_dom_appendToNode(NH_WEBIDL_GET_DOM_NODE(furthest),copy))
        long active=position(&p->ActiveFormattingElements,format);
        if(active>=0){nh_core_removeFromList(&p->ActiveFormattingElements,false,active);if(active<bookmark)bookmark--;}
        NH_CORE_CHECK(nh_core_insertIntoList(&p->ActiveFormattingElements,copy,bookmark))
        nh_core_removeFromList2(&p->OpenElements,false,format);
        NH_CORE_CHECK(nh_core_insertIntoList(&p->OpenElements,copy,position(&p->OpenElements,furthest)+1))
    }
    return NH_API_SUCCESS;
}
