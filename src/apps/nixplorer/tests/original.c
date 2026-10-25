#include "../engine/platform/netzhaut/Probe.h"
#include "../engine/platform/netzhaut/Runtime.h"
#include "../engine/lib/nh-core/System/Memory.h"
#include "../engine/lib/nh-encoding/Base/UnicodeDataHelper.h"
#include "../engine/lib/nh-api/nh-html.h"
#include "../engine/lib/nh-html/Engine/DocumentContext.h"
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

void check_format(void);
void check_network(void);
void check_compat(void);

static void check_allocator_fragmentation(void) {
    unsigned char *slots[257]={0};
    size_t sizes[257]={0};
    uint32_t random=1234567;
    for(unsigned step=0;step<12000;step++) {
        random=random*1664525u+1013904223u;
        unsigned i=random%257;
        unsigned char marker=(unsigned char)(i+1);
        if(slots[i]) {
            for(size_t j=0;j<sizes[i];j++)assert(slots[i][j]==marker);
            if(step%3) { nh_core_free(slots[i]);slots[i]=NULL;continue; }
            size_t size=sizes[i]+513;
            unsigned char *grown=(unsigned char *)nh_core_reallocate(slots[i],size);
            assert(grown);
            for(size_t j=0;j<sizes[i];j++)assert(grown[j]==marker);
            slots[i]=grown;sizes[i]=size;
        } else {
            sizes[i]=1+(random>>8)%4096;
            slots[i]=(unsigned char *)nh_core_allocate(sizes[i]);
            assert(slots[i]&&((uintptr_t)slots[i]%16)==0);
            for(size_t j=0;j<sizes[i];j++)assert(slots[i][j]==0);
        }
        memset(slots[i],marker,sizes[i]);
    }
    for(unsigned i=0;i<257;i++) {
        if(slots[i])for(size_t j=0;j<sizes[i];j++)assert(slots[i][j]==(unsigned char)(i+1));
        nh_core_free(slots[i]);
    }
    assert(nh_nixel_heap_used()==0);
    void *large=nh_core_allocate(12u*1024u*1024u);
    assert(large);nh_core_free(large);
    assert(nh_nixel_heap_used()==0);
}

int main(void) {
    check_allocator_fragmentation();
    check_format();
    check_compat();
    check_network();
    for (int i = 0; i < 64; ++i) assert(nh_nixel_probe());
    void *p = nh_core_allocate(17);
    assert(p && ((uintptr_t)p % 16) == 0);
    ((unsigned char *)p)[16] = 0x7a;
    void *grown = nh_core_reallocate(p, 4096);
    assert(grown && ((unsigned char *)grown)[16] == 0x7a);
    assert(nh_core_reallocate(grown, 32u * 1024u * 1024u) == 0);
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
    assert(nh_nixel_initialize_css());
    printf("CSS initialized (%zu bytes).\n", nh_nixel_heap_used());
    assert(nh_nixel_probe_html());
    printf("Original HTML parser/tree-construction check passed (%zu bytes).\n", nh_nixel_heap_used());
    assert(nh_api_loadBytes(NULL, NULL, 0) == NH_API_ERROR_NULL_POINTER);
    nh_api_DocumentContext *context = nh_api_createDocumentContext(false);
    assert(context);
    assert(nh_api_loadBytes(context, NULL, 1) == NH_API_ERROR_NULL_POINTER);
    assert(nh_api_loadBytes(context, "", ~0ull) == NH_API_ERROR_BAD_STATE);
    nh_api_DocumentContext *browsing = nh_api_createDocumentContext(true);
    assert(browsing);
    assert(((nh_html_DocumentContext *)browsing)->browsingContext);
    assert(nh_api_loadBytes(browsing, "", 0) == NH_API_ERROR_BAD_STATE);
    puts("Original Netzhaut core/encoding and OS allocator checks passed.");
    return 0;
}
