#include "Probe.h"
#include "Runtime.h"
#include "../../lib/nh-core/Util/String.h"
#include "../../lib/nh-core/System/Memory.h"
#include "../../lib/nh-encoding/Encodings/UTF8.h"
#include "../../lib/nh-encoding/Encodings/UTF32.h"
#include <string.h>

int nh_nixel_probe(void) {
    /* Exercise the supplied implementations through their real public types. */
    unsigned char input[] = "Nixel \xD8\xA8 \xF0\x9F\x8C\x90";
    unsigned long read = 0;
    size_t before = nh_nixel_heap_used();
    nh_encoding_UTF32String decoded = nh_encoding_decodeUTF8(input, sizeof(input)-1, &read);
    int ok = read == sizeof(input)-1 && decoded.length == 9 &&
             decoded.p && decoded.p[6] == 0x628 && decoded.p[8] == 0x1f310;
    nh_encoding_UTF8String encoded = nh_encoding_encodeUTF8(decoded.p, decoded.length);
    ok = ok && encoded.length == sizeof(input)-1 &&
         encoded.p && memcmp(encoded.p, input, sizeof(input)-1) == 0;
    nh_encoding_freeUTF8(&encoded);
    nh_encoding_freeUTF32(&decoded);

    /* Covers the original String.c remove function, including its recursion fix. */
    nh_core_String text = nh_core_initString(4);
    if (nh_core_appendToString(&text, "abcd", 4) != NH_API_SUCCESS ||
        nh_core_removeFromString(&text, 1, 2) != NH_API_SUCCESS ||
        text.length != 2 || !text.p || memcmp(text.p, "ad", 2)) ok = 0;
    nh_core_freeString(&text);
    return ok && nh_nixel_heap_used() == before && !nh_nixel_failed();
}
