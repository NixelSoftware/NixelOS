#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../iwlegacy.h"
#include "../../../ieee80211.h"
static void put16(uint8_t* p, unsigned n) { p[0] = n; p[1] = n >> 8; }
int main(void) {
    il3945_eeprom_t e = {0};
    e.channels[0] = (il3945_channel_t){1,2,1,17};
    e.channels[1] = (il3945_channel_t){36,5,1,18};
    for (unsigned g = 0; g < 5; g++) {
        uint8_t* group = e.calibration + 64*g;
        for (unsigned i = 0; i < 5; i++) { group[4*i] = 10 + i*10; group[4*i+1] = 40 - i*10; }
        group[60] = 40; group[61] = 40 + g*20; put16(group+62,(unsigned)-100);
    }
    uint8_t power[52], colder[52];
    assert(il3945_power_table(&e,2,1,-100,17,power) == IL_OK);
    assert(power[0] == 1 && power[2] == 1 && power[4] == 13 && power[4+8*4] == 10);
    assert(il3945_power_table(&e,2,1,-200,17,colder) == IL_OK);
    assert(memcmp(power,colder,sizeof(power)) != 0);
    assert(il3945_power_table(&e,5,36,-100,18,power) == IL_OK && power[0] == 0);
    memset(power,0xA5,sizeof(power));
    assert(il3945_power_table(&e,2,2,-100,17,power) == IL_INVALID && power[0] == 0xA5);
    assert(il3945_power_table(&e,2,1,26,17,power) == IL_INVALID);
    assert(il3945_power_table(&e,2,1,-100,32,power) == IL_INVALID);
    e.calibration[5] = e.calibration[1];
    assert(il3945_power_table(&e,2,1,-100,17,power) == IL_BAD_EEPROM);
    const uint8_t own[6] = {0,1,2,3,4,5}, ap[6] = {2,3,4,5,6,7}, other[6] = {4,5,6,7,8,9};
    uint8_t ethernet[64] = {0}, frame[128], decoded[128]; unsigned written;
    memcpy(ethernet,other,6); memcpy(ethernet+6,own,6); ethernet[12] = 8; ethernet[13] = 0;
    for (unsigned i = 14; i < sizeof(ethernet); i++) ethernet[i] = i;
    assert(wifi_ethernet_encapsulate(own,ap,4095,ethernet,sizeof(ethernet),frame,sizeof(frame),&written) == 0);
    assert(written == 82 && frame[0] == 8 && frame[1] == 1 && frame[22] == 0xF0 && frame[23] == 0xFF);
    assert(!memcmp(frame+4,ap,6) && !memcmp(frame+10,own,6) && !memcmp(frame+16,other,6));
    frame[1] = 2; memcpy(frame+4,own,6); memcpy(frame+10,ap,6); memcpy(frame+16,other,6);
    assert(wifi_ethernet_decapsulate(own,ap,frame,82,decoded,sizeof(decoded),&written) == 0);
    assert(written == 64 && !memcmp(decoded,own,6) && !memcmp(decoded+6,other,6) && !memcmp(decoded+12,ethernet+12,52));
    frame[1] = 0x42;
    assert(wifi_ethernet_decapsulate(own,ap,frame,82,decoded,sizeof(decoded),&written) == -1 && !written);
    frame[1] = 2; frame[10] ^= 1;
    assert(wifi_ethernet_decapsulate(own,ap,frame,82,decoded,sizeof(decoded),&written) == -1);
    frame[10] ^= 1;
    for (unsigned n = 0; n < 32; n++)
        assert(wifi_ethernet_decapsulate(own,ap,frame,n,decoded,sizeof(decoded),&written) == -1);
    assert(wifi_ethernet_encapsulate(own,ap,4096,ethernet,64,frame,128,&written) == -1);
    puts("iwlegacy power/bridge: calibration, temperature, malformed EEPROM and infrastructure framing passed");
    return 0;
}
