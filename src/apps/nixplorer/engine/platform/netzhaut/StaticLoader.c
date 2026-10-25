/* Static symbol resolution for the original WebIDL object initializer loader. */
#include "../../lib/nh-core/Loader/Loader.h"
#include "../../lib/nh-webidl/Runtime/Object.h"
#include <string.h>
#include "../../lib/nh-network/HTTPClient.h"
#include "../../lib/nh-html/Engine/DocumentContext.h"

#define DOM_INITIALIZERS(X) \
    X(Attr) X(CharacterData) X(Comment) X(Document) X(DocumentType) \
    X(Element) X(EventTarget) X(NamedNodeMap) X(Node) X(NodeList)
#define DECLARE(name) extern NH_API_RESULT nh_dom_initialize##name(nh_webidl_Object *);
DOM_INITIALIZERS(DECLARE)
#undef DECLARE
extern NH_API_RESULT nh_html_initializeDocument(nh_webidl_Object *);

#define CSS_INITIALIZERS(X) X(Document) X(StyleSheetList) X(CSSStyleSheet) X(CSSRuleList) X(CSSRule)
#define DECLARE_CSS(name) extern NH_API_RESULT nh_css_initialize##name(nh_webidl_Object *);
CSS_INITIALIZERS(DECLARE_CSS)
#undef DECLARE_CSS

/* These original constructors assign their own payload after createObject;
 * the supplied sources intentionally have no separate initializer for them. */
static NH_API_RESULT constructor_initializes_payload(nh_webidl_Object *object) {
    (void)object;
    return NH_API_SUCCESS;
}

typedef NH_API_RESULT (*initializer_f)(nh_webidl_Object *);
struct entry { NH_MODULE_E module; const char *name; initializer_f function; };
#define ENTRY(name) {NH_MODULE_DOM, "nh_dom_initialize" #name, nh_dom_initialize##name},
static const struct entry entries[] = {
    DOM_INITIALIZERS(ENTRY)
    /* Text has no payload beyond its original CharacterData/Node parents. */
    {NH_MODULE_DOM,"nh_dom_initializeText",constructor_initializes_payload},
#define PASSIVE_HTML(name) {NH_MODULE_HTML,"nh_html_initialize" #name,constructor_initializes_payload},
    PASSIVE_HTML(HTMLElement)
    PASSIVE_HTML(HTMLHtmlElement)
    PASSIVE_HTML(HTMLHeadElement)
    PASSIVE_HTML(HTMLBodyElement)
    PASSIVE_HTML(HTMLStyleElement)
    PASSIVE_HTML(HTMLHeadingElement)
    PASSIVE_HTML(HTMLParagraphElement)
#undef PASSIVE_HTML
#define CSS_ENTRY(name) {NH_MODULE_CSS, "nh_css_initialize" #name, nh_css_initialize##name},
    CSS_INITIALIZERS(CSS_ENTRY)
    {NH_MODULE_CSS,"nh_css_initializeCSSStyleRule",constructor_initializes_payload},
    {NH_MODULE_CSS,"nh_css_initializeCSSStyleDeclaration",constructor_initializes_payload},
    {NH_MODULE_CSS,"nh_css_initializeCSSCounterStyleRule",constructor_initializes_payload},
    {NH_MODULE_CSS,"nh_css_initializeStyleSheet",constructor_initializes_payload},
#undef CSS_ENTRY
    {NH_MODULE_HTML, "nh_html_initializeDocument", nh_html_initializeDocument},
};
#undef ENTRY
int nh_core_getModuleIndex(char *name) {
    static const char *const modules[] = {
        "nh-core", "nh-wsi", "nh-network", "nh-ecmascript", "nh-html",
        "nh-webidl", "nh-encoding", "nh-dom", "nh-gfx", "nh-css",
        "nh-renderer", "nh-url", "nh-monitor"
    };
    if (!name) return -1;
    for (unsigned i = 0; i < sizeof(modules)/sizeof(modules[0]); ++i)
        if (!strcmp(name, modules[i])) return (int)i;
    return -1;
}
void *nh_core_loadExistingSymbol(NH_MODULE_E module, int major, const char *name) {
    if (major != 0 || !name) return 0;
    if(module==NH_MODULE_HTML) {
        if(!strcmp(name,"nh_html_createDocumentContext"))return (void *)nh_html_createDocumentContext;
        if(!strcmp(name,"nh_html_loadBytes"))return (void *)nh_html_loadBytes;
    }
    if(module==NH_MODULE_NETWORK) {
        if(!strcmp(name,"nh_network_performHTTPRequest"))return (void *)nh_network_performHTTPRequest;
        if(!strcmp(name,"nh_network_freeHTTPResponse"))return (void *)nh_network_freeHTTPResponse;
        if(!strcmp(name,"nh_network_getHTTPHeader"))return (void *)nh_network_getHTTPHeader;
    }
    for (unsigned i = 0; i < sizeof(entries)/sizeof(entries[0]); ++i)
        if (entries[i].module == module && !strcmp(entries[i].name, name))
            return (void *)entries[i].function;
    return 0; /* Same missing-symbol result as the original dynamic loader. */
}
