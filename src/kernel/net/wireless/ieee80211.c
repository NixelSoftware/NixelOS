#include "ieee80211.h"
static int equal(const uint8_t* a, const uint8_t* b) {
    for (unsigned i = 0; i < 6; i++) if (a[i] != b[i]) return 0;
    return 1;
}
static void copy(uint8_t* out, const uint8_t* in, unsigned size) {
    for (unsigned i = 0; i < size; i++) out[i] = in[i];
}
int wifi_ethernet_encapsulate(const uint8_t own[6], const uint8_t ap[6], unsigned sequence,
                               const uint8_t* ethernet, unsigned length,
                               uint8_t* frame, unsigned capacity, unsigned* written) {
    if (written) *written = 0;
    if (!own || !ap || !ethernet || !frame || !written || length < 14 || length > 1518 ||
        capacity < length + 18 || sequence > 4095 || !equal(ethernet + 6,own) ||
        ((unsigned)ethernet[12] << 8 | ethernet[13]) < 0x0600 || (own[0] & 1) || (ap[0] & 1)) return -1;
    for (unsigned i = 0; i < 24; i++) frame[i] = 0;
    frame[0] = 8; frame[1] = 1; /* Data, ToDS */
    copy(frame + 4,ap,6); copy(frame + 10,own,6); copy(frame + 16,ethernet,6);
    frame[22] = sequence << 4; frame[23] = sequence >> 4;
    frame[24] = frame[25] = 0xAA; frame[26] = 3; frame[27] = frame[28] = 0;
    unsigned type = (unsigned)ethernet[12] << 8 | ethernet[13];
    frame[29] = type == 0x80F3 || type == 0x8137 ? 0xF8 : 0;
    copy(frame + 30,ethernet + 12,length - 12);
    *written = length + 18; return 0;
}
int wifi_ethernet_decapsulate(const uint8_t own[6], const uint8_t ap[6],
                               const uint8_t* frame, unsigned length,
                               uint8_t* ethernet, unsigned capacity, unsigned* written) {
    if (written) *written = 0;
    if (!own || !ap || !frame || !ethernet || !written || length < 32 ||
        (frame[0] != 8 && frame[0] != 0x88) || (frame[1] & 0xC7) != 2 ||
        (frame[22] & 15) || !equal(frame + 10,ap) ||
        (!(frame[4] & 1) && !equal(frame + 4,own))) return -1;
    unsigned header = frame[0] == 0x88 ? 26 : 24;
    if (length < header + 8 || (header == 26 && (frame[24] & 0x80))) return -1;
    const uint8_t* llc = frame + header;
    unsigned type = (unsigned)llc[6] << 8 | llc[7];
    if (llc[0] != 0xAA || llc[1] != 0xAA || llc[2] != 3 || llc[3] || llc[4] ||
        (llc[5] != 0 && llc[5] != 0xF8) || type < 0x0600 ||
        (!llc[5] && (type == 0x80F3 || type == 0x8137))) return -1;
    unsigned payload = length - header - 8;
    if (payload > 1504 || capacity < payload + 14) return -1;
    copy(ethernet,frame + 4,6); copy(ethernet + 6,frame + 16,6);
    copy(ethernet + 12,llc + 6,payload + 2);
    *written = payload + 14; return 0;
}
