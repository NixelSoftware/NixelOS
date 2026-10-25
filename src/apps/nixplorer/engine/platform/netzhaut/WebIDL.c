#include "WebIDL.h"
#include "Runtime.h"
#include "../../lib/nh-webidl/Utils/Global.h"
#include "../../lib/nh-webidl/Utils/Builtin.h"
#include <string.h>
int nh_nixel_load_fragment(const char *specification, const char *fragment) {
    if (!strcmp(specification,"DOM") && !strcmp(fragment,"CDATASection")) fragment="CDataSection";
    if (nh_webidl_getFragment((char *)specification, (char *)fragment)) return 1;
    unsigned index = 0;
    char name[128];
    size_t spec_length = strlen(specification), fragment_length = strlen(fragment);
    if (spec_length + fragment_length + 2 > sizeof(name)) return 0;
    memcpy(name, specification, spec_length);
    name[spec_length] = '_';
    memcpy(name + spec_length + 1, fragment, fragment_length + 1);
    for (; index < NH_WEBIDL_FRAGMENT_NAMES_PP_COUNT; ++index)
        if (!strcmp(NH_WEBIDL_FRAGMENT_NAMES_PP[index], name)) break;
    if (index == NH_WEBIDL_FRAGMENT_NAMES_PP_COUNT) return 0;
    nh_webidl_FragmentParseResult parsed = nh_webidl_parseBytes(name,
        (char *)NH_WEBIDL_FRAGMENTS_PP[index], NH_WEBIDL_FRAGMENT_LENGTHS_P[index]);
    if (!parsed.Root_p || parsed.SyntaxErrors.size || nh_nixel_failed()) return 0;
    return nh_webidl_load((char *)specification, (char *)fragment, parsed) == NH_API_SUCCESS;
}
