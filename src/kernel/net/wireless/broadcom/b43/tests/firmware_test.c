#include <assert.h>
#include <stdio.h>
#include "../b43.h"
static unsigned writes;
static int writer(void* ctx, uint16_t offset, uint32_t value, uint8_t width) {
    assert(ctx == &writes);
    if (!writes) { assert(offset == 0x100 && value == 0x1234 && width == 2); }
    else { assert(offset == 0x104 && value == 0x12345678 && width == 4); }
    writes++; return 1;
}
static int reject(void* ctx, uint16_t o, uint32_t v, uint8_t w) {
    (void)ctx; (void)o; (void)v; (void)w; writes++; return 0;
}
int main(void) {
    uint8_t iv[] = {'i',1,0,0,0,0,0,2, 1,0,0x12,0x34, 0x81,4,0x12,0x34,0x56,0x78};
    uint8_t ucode[] = {'u',1,0,0,0,0,0,4, 1,2,3,4};
    b43_firmware_t fw;
    assert(b43_firmware_parse(iv,sizeof iv,'i',&fw));
    assert(b43_firmware_initvals(&fw,writer,&writes) && writes == 2);
    writes = 0;
    assert(!b43_firmware_initvals(&fw,reject,0) && writes == 1);
    for (unsigned n = 0; n < sizeof iv; n++) {
        assert(!b43_firmware_parse(iv,n,'i',&fw));
        assert(!fw.data && !fw.count && !fw.length);
    }
    assert(!b43_firmware_parse(0,8,'i',&fw));
    assert(!b43_firmware_parse(iv,sizeof iv,'u',&fw));
    assert(!b43_firmware_parse(iv,sizeof iv,'i',0));
    iv[1] = 2; assert(!b43_firmware_parse(iv,sizeof iv,'i',&fw)); iv[1] = 1;
    iv[7] = 1; assert(!b43_firmware_parse(iv,sizeof iv,'i',&fw)); iv[7] = 2;
    iv[12] = 0x90; assert(!b43_firmware_parse(iv,sizeof iv,'i',&fw)); iv[12] = 0x81;
    iv[13] = 5; assert(!b43_firmware_parse(iv,sizeof iv,'i',&fw)); iv[13] = 4;
    assert(b43_firmware_parse(iv,sizeof iv,'i',&fw));
    iv[12] = 0x90; writes = 0;
    assert(!b43_firmware_initvals(&fw,writer,&writes) && writes == 0);
    assert(b43_firmware_parse(ucode,sizeof ucode,'u',&fw));
    for (unsigned n = 0; n < sizeof ucode; n++) assert(!b43_firmware_parse(ucode,n,'u',&fw));
    ucode[7] = 3; assert(!b43_firmware_parse(ucode,sizeof ucode,'u',&fw));
    ucode[7] = 4; ucode[0] = 'p'; assert(b43_firmware_parse(ucode,sizeof ucode,'p',&fw));
    assert(!b43_firmware_initvals(&fw,writer,&writes));
    puts("b43 firmware validation passed");
}
