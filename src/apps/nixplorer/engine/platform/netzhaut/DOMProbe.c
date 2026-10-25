/* Bring-up probe using an actual supplied IDL fragment and original DOM code. */
#include "Probe.h"
#include "Runtime.h"
#include "../../lib/nh-webidl/Utils/Global.h"
#include "../../lib/nh-webidl/Utils/Builtin.h"
#include "../../lib/nh-webidl/Common/IndexMap.h"
#include "../../lib/nh-webidl/Runtime/Object.h"
#include "../../lib/nh-dom/Interfaces/NodeList.h"
#include "../../lib/nh-dom/Interfaces/Node.h"
#include "../../lib/nh-dom/Interfaces/Text.h"
#include "../../lib/nh-webidl/Runtime/String.h"
#include <string.h>

static int load_dom_fragment(char *fragment) {
    unsigned index = 0;
    char name[80] = "DOM_";
    if (strlen(fragment) >= sizeof(name)-4) return 0;
    strcpy(name+4, fragment);
    for (; index < NH_WEBIDL_FRAGMENT_NAMES_PP_COUNT; ++index)
        if (!strcmp(NH_WEBIDL_FRAGMENT_NAMES_PP[index], name)) break;
    if (index == NH_WEBIDL_FRAGMENT_NAMES_PP_COUNT) return 0;
    nh_webidl_FragmentParseResult parsed = nh_webidl_parseBytes(
        name, (char *)NH_WEBIDL_FRAGMENTS_PP[index], NH_WEBIDL_FRAGMENT_LENGTHS_P[index]);
    if (!parsed.Root_p || parsed.SyntaxErrors.size || nh_nixel_failed()) return 0;
    return nh_webidl_load("DOM", fragment, parsed) == NH_API_SUCCESS;
}
int nh_nixel_probe_dom(void) {
    if (nh_webidl_createIndexMap() != NH_API_SUCCESS ||
        nh_webidl_initGlobal() != NH_API_SUCCESS || !load_dom_fragment("NodeList")) return 0;
    nh_webidl_Interface *interface = nh_webidl_getInterface("DOM", "NodeList");
    if (!interface || interface->Members.length < 2) return 0;
    nh_webidl_Object *list = nh_dom_createNodeList();
    nh_webidl_Object *child = nh_dom_createNodeList();
    if (!list || !child || list->Interface_p != interface) return 0;
    if (nh_dom_appendToNodeList(list, child) != NH_API_SUCCESS) return 0;
    int ok = NH_WEBIDL_GET_DOM_NODEListLength(list) == 1 &&
           nh_dom_getFromNodeList(list, 0) == child &&
           nh_webidl_getObject(list, "DOM", "NodeList") == list &&
           !nh_webidl_getObject(list, "DOM", "Element") && !nh_nixel_failed();
    if (!ok || !load_dom_fragment("EventTarget") || !load_dom_fragment("Node") ||
        !load_dom_fragment("CharacterData") || !load_dom_fragment("Text")) return 0;
    nh_webidl_DOMString text = nh_webidl_initDOMString(16);
    char bytes[] = "Netzhaut \xD8\xA8";
    if (nh_webidl_appendToDOMString(&text, bytes, sizeof(bytes)-1) != NH_API_SUCCESS) return 0;
    nh_webidl_Object *node = nh_dom_createText(text, NULL);
    nh_webidl_freeDOMString(&text);
    if (!node || !nh_webidl_getObject(node, "DOM", "CharacterData") ||
        !nh_webidl_getObject(node, "DOM", "EventTarget")) return 0;
    nh_encoding_UTF8String *content = nh_dom_getTextString(node);
    return content && content->length == sizeof(bytes)-1 &&
           !memcmp(content->p, bytes, sizeof(bytes)-1) &&
           nh_dom_getNodeDocument(NH_WEBIDL_GET_DOM_NODE(node)) == NULL && !nh_nixel_failed();
}
