/* SPDX-License-Identifier: GPL-3.0-or-later
 * CCMP-128 framing and replay protection over the existing wolfCrypt AES-CCM.
 */
#include "ccmp.h"
#include <wolfssl/wolfcrypt/aes.h>
#define PN_MAX 0xFFFFFFFFFFFFull
#define MSDU_MAX 2304u
static int same(const uint8_t* a, const uint8_t* b, unsigned length) {
    unsigned difference = 0;
    for (unsigned i = 0; i < length; i++) difference |= a[i] ^ b[i];
    return difference == 0;
}
static int unicast(const uint8_t* mac) {
    unsigned any = 0; for (unsigned i = 0; i < 6; i++) any |= mac[i];
    return any && !(mac[0] & 1);
}
int wifi_ccmp_install(wifi_ccmp_key_t* k, const uint8_t key[16], unsigned key_id,
                        const uint8_t own[6], const uint8_t peer[6]) {
    if (!k || !key || !own || !peer || key_id > 3 || !unicast(own) || !unicast(peer)) return -1;
    if (k->active && same(k->key,key,16))
        return k->key_id == key_id && same(k->own,own,6) && same(k->peer,peer,6) ? 0 : -1;
    wifi_crypto_erase(k,sizeof(*k));
    wifi_crypto_memcpy(k->key,key,16); wifi_crypto_memcpy(k->own,own,6); wifi_crypto_memcpy(k->peer,peer,6);
    k->key_id = key_id; k->next_tx_pn = 1; k->active = 1;
    return 0;
}
static int framing(const uint8_t* p, unsigned length, unsigned* header, unsigned* tid) {
    if (!p || length < 24 || (p[0] & 15) != 8 || ((p[0] & 0xF0) != 0 && (p[0] & 0xF0) != 0x80) ||
        (p[1] & 3) == 3 || (p[1] & 0x84) || (p[22] & 15)) return -1;
    *header = p[0] & 0x80 ? 26 : 24;
    if (length < *header) return -1;
    *tid = *header == 26 ? p[24] & 15 : 16;
    return 0;
}
static unsigned authenticated_header(const uint8_t* p, unsigned header, uint8_t aad[24]) {
    aad[0] = p[0] & 0x8F; aad[1] = p[1] & 0xC7;
    wifi_crypto_memcpy(aad + 2,p + 4,18); aad[20] = p[22] & 15; aad[21] = 0;
    if (header == 26) { aad[22] = p[24] & 15; aad[23] = 0; return 24; }
    return 22;
}
static void make_nonce(const uint8_t* p, unsigned tid, uint64_t pn, uint8_t nonce[13]) {
    nonce[0] = tid == 16 ? 0 : tid;
    wifi_crypto_memcpy(nonce + 1,p + 10,6);
    for (unsigned i = 0; i < 6; i++) nonce[7 + i] = (uint8_t)(pn >> ((5 - i) * 8));
}
int wifi_ccmp_encrypt(wifi_ccmp_key_t* k, const uint8_t* frame, unsigned length,
                        uint8_t* out, unsigned capacity, unsigned* written) {
    if (written) *written = 0;
    unsigned header, tid;
    if (!written || !k || !k->active || !out || framing(frame,length,&header,&tid) ||
        (frame[1] & 0x40) || length - header > MSDU_MAX || length + 16 > capacity ||
        !same(frame + 10,k->own,6) || !k->next_tx_pn || k->next_tx_pn > PN_MAX) return -1;
    uint64_t pn = k->next_tx_pn++; /* A failed attempt also burns the nonce. */
    wifi_crypto_memcpy(out,frame,header); out[1] |= 0x40;
    uint8_t* iv = out + header;
    iv[0] = pn; iv[1] = pn >> 8; iv[2] = 0; iv[3] = (k->key_id << 6) | 0x20;
    for (unsigned i = 0; i < 4; i++) iv[4 + i] = pn >> (16 + i * 8);
    uint8_t nonce[13], aad[24];
    make_nonce(out,tid,pn,nonce); unsigned aad_length = authenticated_header(out,header,aad);
    Aes aes; int result = wc_AesInit(&aes,0,INVALID_DEVID);
    if (!result) {
        result = wc_AesCcmSetKey(&aes,k->key,16);
        if (!result) result = wc_AesCcmEncrypt(&aes,out + header + 8,frame + header,length - header,
            nonce,13,out + length + 8,8,aad,aad_length);
        wc_AesFree(&aes);
    }
    wifi_crypto_erase(&aes,sizeof(aes));
    if (result) { wifi_crypto_erase(out,length + 16); return -1; }
    *written = length + 16;
    return 0;
}
int wifi_ccmp_decrypt(wifi_ccmp_key_t* k, const uint8_t* frame, unsigned length,
                        uint8_t* out, unsigned capacity, unsigned* written) {
    if (written) *written = 0;
    unsigned header, tid;
    if (!written || !k || !k->active || !out || framing(frame,length,&header,&tid) ||
        !(frame[1] & 0x40) || length < header + 16 || length - header - 16 > MSDU_MAX ||
        capacity < length - 16 || !same(frame + 10,k->peer,6)) return -1;
    const uint8_t* iv = frame + header;
    if (iv[2] || (iv[3] & 63) != 0x20 || (iv[3] >> 6) != k->key_id) return -1;
    uint64_t pn = (uint64_t)iv[0] | (uint64_t)iv[1] << 8;
    for (unsigned i = 0; i < 4; i++) pn |= (uint64_t)iv[4 + i] << (16 + i * 8);
    if (!pn || pn <= k->rx_pn[tid]) return -1;
    uint8_t plaintext[MSDU_MAX], nonce[13], aad[24];
    unsigned payload = length - header - 16;
    make_nonce(frame,tid,pn,nonce); unsigned aad_length = authenticated_header(frame,header,aad);
    Aes aes; int result = wc_AesInit(&aes,0,INVALID_DEVID);
    if (!result) {
        result = wc_AesCcmSetKey(&aes,k->key,16);
        if (!result) result = wc_AesCcmDecrypt(&aes,plaintext,frame + header + 8,payload,
            nonce,13,frame + length - 8,8,aad,aad_length);
        wc_AesFree(&aes);
    }
    wifi_crypto_erase(&aes,sizeof(aes));
    if (!result) {
        wifi_crypto_memcpy(out,frame,header); out[1] &= ~0x40;
        wifi_crypto_memcpy(out + header,plaintext,payload);
        k->rx_pn[tid] = pn; *written = length - 16;
    }
    wifi_crypto_erase(plaintext,sizeof(plaintext));
    return result ? -1 : 0;
}
int wifi_wpa2_unwrap(const uint8_t kek[16], const uint8_t* wrapped, unsigned length,
                       uint8_t* out, unsigned capacity, unsigned* written) {
    if (written) *written = 0;
    if (!written || !kek || !wrapped || !out || length < 24 || length > 512 ||
        (length & 7) || capacity < length - 8) return -1;
    int result = wc_AesKeyUnWrap(kek,16,wrapped,length,out,capacity,0);
    if (result < 0) { wifi_crypto_erase(out,length - 8); return -1; }
    *written = (unsigned)result;
    return 0;
}
