/* DOM selectors use the original CSS tokenizer, parser and matcher. */
#include "DOM.h"
#include "../../nh-css/Parser/Parser.h"
#include "../../nh-css/Properties/Match.h"
#include "../../nh-core/System/Memory.h"
#include <string.h>
static void free_tree(nh_css_SelectorParseNode *n) {
    if(!n)return;
    for(unsigned long i=0;i<n->Children.size;i++)free_tree(n->Children.pp[i]);
    nh_core_freeList(&n->Children,false);nh_core_free(n);
}
static void free_components(nh_core_Array *array) {
    for(unsigned long i=0;i<array->length;i++) {
        nh_css_ComponentValue *v=(nh_css_ComponentValue *)array->p+i;
        if(v->type==NH_CSS_COMPONENT_VALUE_SIMPLE_BLOCK)free_components(&v->Block.ComponentValues);
        else if(v->type==NH_CSS_COMPONENT_VALUE_FUNCTION)free_components(&v->Function.ComponentValues);
    }
    nh_core_freeArray(array);
}
static void free_data(nh_css_ComponentValueData *data) {
    free_components(&data->ComponentValues);
    for(unsigned long i=0;i<data->Tokens.length;i++) {
        nh_css_Token *t=(nh_css_Token *)data->Tokens.p+i;
        switch(t->type) {
            case NH_CSS_TOKEN_IDENT:case NH_CSS_TOKEN_FUNCTION:case NH_CSS_TOKEN_AT_KEYWORD:
            case NH_CSS_TOKEN_HASH:case NH_CSS_TOKEN_STRING:case NH_CSS_TOKEN_URL:
                nh_encoding_freeUTF32(&t->Other.Value);break;
            case NH_CSS_TOKEN_DIMENSION:nh_encoding_freeUTF32(&t->Dimension.Unit);break;
            default:break;
        }
    }
    nh_core_freeArray(&data->Tokens);
}
static NH_API_RESULT query(nh_webidl_Object *root,nh_css_SelectorParseNode *tree,nh_core_List *out,bool first) {
    nh_core_List *children=nh_dom_getNodeChildren(NH_WEBIDL_GET_DOM_NODE(root));
    for(unsigned long i=0;i<children->size;i++) {
        nh_webidl_Object *o=children->pp[i],*element=NH_WEBIDL_GET_DOM_ELEMENT(o);
        if(element&&nh_css_matchSelectors(tree,element)) {
            NH_CORE_CHECK(nh_core_appendToList(out,o))
            if(first)return NH_API_SUCCESS;
        }
        NH_CORE_CHECK(query(o,tree,out,first))
        if(first&&out->size)return NH_API_SUCCESS;
    }
    return NH_API_SUCCESS;
}
static nh_css_SelectorParseNode *parse(const char *selector,nh_css_ComponentValueData *data) {
    *data=nh_css_parseComponentValuesFromUTF8Codepoints(NULL,(char *)selector,strlen(selector));
    return nh_css_parseSelectorList(data->ComponentValues);
}
NH_API_RESULT nh_dom_querySelectorAll(nh_webidl_Object *root,const char *selector,nh_core_List *out) {
    if(!root||!selector||!out)return NH_API_ERROR_NULL_POINTER;
    if(!selector[0]||!NH_WEBIDL_GET_DOM_NODE(root))return NH_API_ERROR_BAD_STATE;
    nh_css_ComponentValueData data;nh_css_SelectorParseNode *tree=parse(selector,&data);
    NH_API_RESULT status=tree?query(root,tree,out,false):NH_API_ERROR_BAD_STATE;
    free_tree(tree);free_data(&data);return status;
}
nh_webidl_Object *nh_dom_querySelector(nh_webidl_Object *root,const char *selector) {
    if(!root||!selector||!selector[0]||!NH_WEBIDL_GET_DOM_NODE(root))return NULL;
    nh_css_ComponentValueData data;nh_css_SelectorParseNode *tree=parse(selector,&data);
    nh_core_List found=nh_core_initList(1);
    if(tree)query(root,tree,&found,true);
    nh_webidl_Object *o=found.size?found.pp[0]:NULL;
    nh_core_freeList(&found,false);free_tree(tree);free_data(&data);return o;
}
NH_API_RESULT nh_dom_matches(nh_webidl_Object *o,const char *selector,bool *match) {
    if(!o||!selector||!match)return NH_API_ERROR_NULL_POINTER;
    if(!selector[0]||!NH_WEBIDL_GET_DOM_ELEMENT(o))return NH_API_ERROR_BAD_STATE;
    nh_css_ComponentValueData data;nh_css_SelectorParseNode *tree=parse(selector,&data);
    *match=tree&&nh_css_matchSelectors(tree,NH_WEBIDL_GET_DOM_ELEMENT(o));
    free_tree(tree);free_data(&data);return tree?NH_API_SUCCESS:NH_API_ERROR_BAD_STATE;
}
