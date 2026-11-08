#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "../bss.h"
int main(void) {
    il_network_t ap={0}; ap.band=2; ap.capability=1|32|1024;
    const uint8_t ies[]={1,2,0x82,12,50,1,24,42,1,6};
    assert(!il3945_bss_parse(&ap,ies,sizeof(ies),1));
    assert(ap.rate_count==3 && ap.erp_valid && ap.erp==6 && il3945_bss_has_ofdm(&ap));
    unsigned flags=il3945_bss_flags(&ap);
    assert((flags&(1u<<3)) && (flags&(1u<<30)) && !(flags&(1u<<5)) && (flags&(1u<<4)));
    il_network_t before=ap;
    const uint8_t truncated[]={1,1,0x82,42,2,0};
    assert(il3945_bss_parse(&ap,truncated,sizeof(truncated),1)==IL_VERIFY);
    assert(!memcmp(&ap,&before,sizeof(ap)));
    const uint8_t duplicate[]={1,1,0x82,1,1,0x84};
    assert(il3945_bss_parse(&ap,duplicate,sizeof(duplicate),1)==IL_VERIFY);
    assert(il3945_bss_parse(&ap,0,0,1)==IL_VERIFY);
    const uint8_t erp[]={42,1,0};
    assert(!il3945_bss_parse(&ap,erp,sizeof(erp),0));
    assert(il3945_bss_flags(&ap)&32);
    ap.band=5;
    assert(!(il3945_bss_flags(&ap)&((1u<<3)|(1u<<30))));
    puts("iwlegacy BSS: bounded IEs, transactional failure, rates and ERP precedence passed");
}
