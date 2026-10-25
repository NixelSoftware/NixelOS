/* Exercise the supplied HTML parser and tree constructor, never a fallback. */
#include "Probe.h"
#include "Runtime.h"
#include "WebIDL.h"
#include "../../lib/nh-html/Common/IndexMap.h"
#include "../../lib/nh-html/Interfaces/Document.h"
#include "../../lib/nh-html/Parser/Parser.h"
#include "../../lib/nh-dom/Interfaces/Document.h"
#include "../../lib/nh-dom/Interfaces/Node.h"
#include "../../lib/nh-dom/Interfaces/Element.h"
#include "../../lib/nh-dom/Interfaces/Text.h"
#include "../../lib/nh-webidl/Runtime/String.h"
#include "../../lib/nh-encoding/Encodings/UTF8.h"
#include <string.h>

static nh_webidl_Object *child_element(nh_webidl_Object *parent, const char *name) {
    nh_core_List *children=nh_dom_getNodeChildren(NH_WEBIDL_GET_DOM_NODE(parent));
    for(unsigned i=0;children && i<children->size;++i) {
        nh_webidl_Object *element=NH_WEBIDL_GET_DOM_ELEMENT(children->pp[i]);
        if(element && !strcmp(nh_dom_getLocalName(element)->p,name))return children->pp[i];
    }
    return NULL;
}
int nh_nixel_probe_html(void) {
    const char *dom[] = {"Document", "Element", "NamedNodeMap", "Attr", "DocumentType", "Comment"};
    const char *html[] = {"Document", "HTMLElement", "HTMLHtmlElement", "HTMLHeadElement", "HTMLBodyElement"};
    for (unsigned i=0;i<sizeof(dom)/sizeof(dom[0]);++i)
        if (!nh_nixel_load_fragment("DOM",dom[i])) return 0;
    for (unsigned i=0;i<sizeof(html)/sizeof(html[0]);++i)
        if (!nh_nixel_load_fragment("HTML",html[i])) return 0;
    if (nh_html_createIndexMap()!=NH_API_SUCCESS) return 0;
    if (nh_webidl_initNamespaces()!=NH_API_SUCCESS || nh_dom_initDocumentModes()!=NH_API_SUCCESS) return 0;
    nh_webidl_Object *document = nh_html_createDocument(NULL);
    if (!document) return 0;
    unsigned char source[] = "<!doctype html><html><head></head><body>Netzhaut \xD8\xA8</body></html>";
    nh_encoding_UTF32String input = nh_encoding_decodeUTF8(source, sizeof(source)-1, NULL);
    if (!input.p || nh_html_parseDocument(NULL,input,document)!=NH_API_SUCCESS) return 0;
    nh_encoding_freeUTF32(&input);
    nh_webidl_Object *root=child_element(document,"html");
    if(!root || !child_element(root,"head"))return 0;
    nh_webidl_Object *body=child_element(root,"body");
    if(!body)return 0;
    nh_core_List *children=nh_dom_getNodeChildren(NH_WEBIDL_GET_DOM_NODE(body));
    if(!children || children->size!=1)return 0;
    nh_webidl_Object *text=NH_WEBIDL_GET_DOM_TEXT(children->pp[0]);
    if(!text)return 0;
    nh_encoding_UTF8String *content=nh_dom_getTextString(text);
    const char expected[]="Netzhaut \xD8\xA8";
    return content && content->length==sizeof(expected)-1 &&
           !memcmp(content->p,expected,sizeof(expected)-1) && !nh_nixel_failed();
}
