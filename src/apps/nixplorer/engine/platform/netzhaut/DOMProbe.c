/* Bring-up probe using an actual supplied IDL fragment and original DOM code. */
#include "Probe.h"
#include "WebIDL.h"
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

int nh_nixel_probe_dom(void) {
    if (nh_webidl_createIndexMap() != NH_API_SUCCESS ||
        nh_webidl_initGlobal() != NH_API_SUCCESS || !nh_nixel_load_fragment("DOM", "NodeList")) return 0;
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
    if (!ok || !nh_nixel_load_fragment("DOM", "EventTarget") || !nh_nixel_load_fragment("DOM", "Node") ||
        !nh_nixel_load_fragment("DOM", "CharacterData") || !nh_nixel_load_fragment("DOM", "Text")) return 0;
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
