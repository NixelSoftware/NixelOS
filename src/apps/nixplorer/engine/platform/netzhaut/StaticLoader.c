/* Static symbol resolution for the original WebIDL object initializer loader. */
#include "../../lib/nh-core/Loader/Loader.h"
#include "../../lib/nh-webidl/Runtime/Object.h"
#include <string.h>
#include "../../lib/nh-network/HTTPClient.h"

#define DOM_INITIALIZERS(X) \
    X(Attr) X(CharacterData) X(Comment) X(Document) X(DocumentType) \
    X(Element) X(EventTarget) X(NamedNodeMap) X(Node) X(NodeList)
#define DECLARE(name) extern NH_API_RESULT nh_dom_initialize##name(nh_webidl_Object *);
DOM_INITIALIZERS(DECLARE)
#undef DECLARE
extern NH_API_RESULT nh_html_initializeDocument(nh_webidl_Object *);

typedef NH_API_RESULT (*initializer_f)(nh_webidl_Object *);
struct entry { NH_MODULE_E module; const char *name; initializer_f function; };
#define ENTRY(name) {NH_MODULE_DOM, "nh_dom_initialize" #name, nh_dom_initialize##name},
static const struct entry entries[] = {
    DOM_INITIALIZERS(ENTRY)
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
