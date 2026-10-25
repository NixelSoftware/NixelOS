/* Load original CSS registry fragments and initialize the original cascade. */
#include "Probe.h"
#include "WebIDL.h"
#include "../../lib/nh-css/Common/Initialize.h"
int nh_nixel_initialize_css(void) {
    static int initialized;
    if (initialized) return 1;
    const char *names[]={"Document","StyleSheetList","StyleSheet","CSSStyleSheet","CSSRuleList",
        "CSSRule","CSSStyleRule","CSSCounterStyleRule","CSSStyleDeclaration"};
    for(unsigned i=0;i<sizeof(names)/sizeof(*names);i++)
        if(!nh_nixel_load_fragment("CSS",names[i])) return 0;
    initialized=nh_css_initialize()==NH_API_SUCCESS;
    return initialized;
}

#include "../../lib/nh-html/Engine/DocumentContext.h"
#include "../../lib/nh-dom/Interfaces/Node.h"
#include "../../lib/nh-dom/Interfaces/Element.h"
#include "../../lib/nh-dom/Interfaces/Text.h"
#include "../../lib/nh-css/Interfaces/Document.h"
#include "../../lib/nh-css/Interfaces/StyleSheetList.h"
#include "../../lib/nh-css/Parser/Parser.h"
#include "../../lib/nh-css/Interfaces/StyleSheet.h"
#include "../../lib/nh-core/Util/String.h"
#include <string.h>
static NH_API_RESULT collect_styles(nh_webidl_Object *object,nh_webidl_Object *sheets,unsigned depth) {
    if(depth>256)return NH_API_ERROR_BAD_STATE;
    nh_webidl_Object *element=NH_WEBIDL_GET_DOM_ELEMENT(object);
    nh_core_List *children=nh_dom_getNodeChildren(NH_WEBIDL_GET_DOM_NODE(object));
    if(element&&!strcmp(nh_dom_getLocalName(element)->p,"style")) {
        nh_core_String source=nh_core_initString(128);
        for(unsigned i=0;children&&i<children->size;i++) {
            nh_webidl_Object *text=NH_WEBIDL_GET_DOM_TEXT(children->pp[i]);
            if(text) {
                nh_encoding_UTF8String *s=nh_dom_getTextString(text);
                NH_CORE_CHECK(nh_core_appendToString(&source,s->p,s->length))
            }
        }
        nh_encoding_UTF32String input=nh_encoding_decodeUTF8((unsigned char *)source.p,source.length,NULL);
        nh_webidl_Object *sheet=nh_css_parseStyleSheetFromUTF32(&input,NULL);
        nh_encoding_freeUTF32(&input);
        nh_core_freeString(&source);
        if(!sheet)return NH_API_ERROR_BAD_STATE;
        NH_CORE_CHECK(nh_css_appendToStyleSheetList(sheets,sheet))
        return NH_API_SUCCESS;
    }
    for(unsigned i=0;children&&i<children->size;i++) {
        NH_CORE_CHECK(collect_styles(children->pp[i],sheets,depth+1))
    }
    return NH_API_SUCCESS;
}
NH_API_RESULT nh_nixel_load_document_styles(nh_html_DocumentContext *context) {
    if(!context||!context->Document_p)return NH_API_ERROR_NULL_POINTER;
    nh_webidl_Object *css=nh_css_getDocument(context->Document_p);
    if(!css)return NH_API_ERROR_BAD_STATE;
    return collect_styles(context->Document_p,nh_css_getStyleSheetList(css),0);
}
