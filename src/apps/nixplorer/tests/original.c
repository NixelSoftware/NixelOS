#include "../engine/platform/netzhaut/Probe.h"
#include "../engine/platform/netzhaut/Runtime.h"
#include "../engine/lib/nh-core/System/Memory.h"
#include "../engine/lib/nh-encoding/Base/UnicodeDataHelper.h"
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

void check_format(void);
void check_network(void);
void check_compat(void);

int main(void) {
    check_format();
    check_compat();
    check_network();
    for (int i = 0; i < 64; ++i) assert(nh_nixel_probe());
    void *p = nh_core_allocate(17);
    assert(p && ((uintptr_t)p % 16) == 0);
    ((unsigned char *)p)[16] = 0x7a;
    void *grown = nh_core_reallocate(p, 4096);
    assert(grown && ((unsigned char *)grown)[16] == 0x7a);
    assert(nh_core_reallocate(grown, 4u * 1024u * 1024u) == 0);
    assert(((unsigned char *)grown)[16] == 0x7a);
    nh_core_free(grown);
    assert(nh_nixel_heap_used() == 0);
    nh_nixel_reset(); /* No live engine allocations exist before registry initialization. */
    assert(nh_nixel_probe_dom());
    const char *arabic = nh_encoding_getDescription(0x628);
    assert(arabic && !strncmp(arabic, "0628;ARABIC LETTER BEH;Lo;", 24));
    const char *latin = nh_encoding_getDescription('A');
    assert(latin && !strncmp(latin, "0041;LATIN CAPITAL LETTER A;Lu;", 30));
    assert(nh_encoding_getDescription(0x378) == 0);
    assert(nh_encoding_getDescription(0x110000) == 0);
    assert(nh_encoding_getDescription(0x628) == arabic);
    printf("Original WebIDL parser/runtime and DOM inheritance/text checks passed (%zu bytes).\n", nh_nixel_heap_used());
    assert(nh_nixel_probe_html());
    printf("Original HTML parser/tree-construction check passed (%zu bytes).\n", nh_nixel_heap_used());
    puts("Original Netzhaut core/encoding and OS allocator checks passed.");
    return 0;
}
