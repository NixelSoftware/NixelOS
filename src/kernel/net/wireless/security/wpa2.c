/* SPDX-License-Identifier: GPL-3.0-or-later
 * WPA2-PSK key derivation using the existing wolfCrypt SHA-1/HMAC/PBKDF2.
 * wolfSSL sources retain their upstream copyright and licensing notices.
 */
#include "wpa2.h"
#include <wolfssl/wolfcrypt/hmac.h>
#include <wolfssl/wolfcrypt/pwdbased.h>
void* wifi_crypto_memcpy(void* out, const void* in, size_t length) {
    uint8_t* d = out; const uint8_t* s = in;
    while (length--) *d++ = *s++;
    return out;
}
void* wifi_crypto_memmove(void* out, const void* in, size_t length) {
    uint8_t* d = out; const uint8_t* s = in;
    if ((unsigned long)d < (unsigned long)s) return wifi_crypto_memcpy(out,in,length);
    while (length) { length--; d[length] = s[length]; }
    return out;
}
void* wifi_crypto_memset(void* out, int value, size_t length) {
    uint8_t* d = out; while (length--) *d++ = (uint8_t)value; return out;
}
int wifi_crypto_memcmp(const void* a, const void* b, size_t length) {
    const uint8_t* x = a; const uint8_t* y = b;
    while (length--) { if (*x != *y) return *x - *y; x++; y++; } return 0;
}
size_t wifi_crypto_strlen(const char* text) {
    size_t n = 0; while (text[n]) n++; return n;
}
void wifi_crypto_zero(void* memory, size_t length) {
    volatile uint8_t* p = memory;
    while (length--) *p++ = 0;
}
void wifi_crypto_erase(void* memory, unsigned length) { wifi_crypto_zero(memory,length); }
int wifi_wpa2_pmk(const uint8_t* password, unsigned password_length,
                   const uint8_t* ssid, unsigned ssid_length, uint8_t pmk[32]) {
    if (!pmk) return -1;
    wifi_crypto_erase(pmk,32);
    if (!password || !ssid || !ssid_length || ssid_length > 32 ||
        password_length < 8 || password_length > 63) return -1;
    for (unsigned i = 0; i < password_length; i++)
        if (password[i] < 32 || password[i] > 126) return -1;
    int result = wc_PBKDF2(pmk,password,(int)password_length,ssid,(int)ssid_length,4096,32,WC_SHA);
    if (result) wifi_crypto_erase(pmk,32);
    return result ? -1 : 0;
}
int wifi_wpa2_ptk(const uint8_t pmk[32], const uint8_t own_mac[6], const uint8_t ap_mac[6],
                   const uint8_t snonce[32], const uint8_t anonce[32], uint8_t ptk[64]) {
    if (!ptk) return -1;
    wifi_crypto_erase(ptk,64);
    if (!pmk || !own_mac || !ap_mac || !snonce || !anonce) return -1;
    uint8_t context[76], digest[WC_SHA_DIGEST_SIZE];
    int own_first = wifi_crypto_memcmp(own_mac,ap_mac,6) < 0;
    int snonce_first = wifi_crypto_memcmp(snonce,anonce,32) < 0;
    wifi_crypto_memcpy(context,own_first ? own_mac : ap_mac,6);
    wifi_crypto_memcpy(context + 6,own_first ? ap_mac : own_mac,6);
    wifi_crypto_memcpy(context + 12,snonce_first ? snonce : anonce,32);
    wifi_crypto_memcpy(context + 44,snonce_first ? anonce : snonce,32);
    Hmac hmac;
    int result = wc_HmacInit(&hmac,0,INVALID_DEVID);
    if (result) goto done;
    result = wc_HmacSetKey(&hmac,WC_SHA,pmk,32);
    for (uint8_t counter = 0; !result && counter < 4; counter++) {
        /* sizeof includes the PRF label's required zero separator. */
        static const uint8_t label[] = "Pairwise key expansion";
        result = wc_HmacUpdate(&hmac,label,sizeof(label));
        if (!result) result = wc_HmacUpdate(&hmac,context,sizeof(context));
        if (!result) result = wc_HmacUpdate(&hmac,&counter,1);
        if (!result) result = wc_HmacFinal(&hmac,digest);
        if (!result) {
            unsigned offset = counter * 20, count = 64 - offset;
            if (count > 20) count = 20;
            wifi_crypto_memcpy(ptk + offset,digest,count);
        }
    }
    wc_HmacFree(&hmac);
done:
    wifi_crypto_erase(&hmac,sizeof(hmac));
    wifi_crypto_erase(context,sizeof(context)); wifi_crypto_erase(digest,sizeof(digest));
    if (result) wifi_crypto_erase(ptk,64);
    return result ? -1 : 0;
}
int wifi_wpa2_eapol_mic(const uint8_t kck[16], const uint8_t* frame,
                         unsigned length, uint8_t mic[16]) {
    if (!mic) return -1;
    wifi_crypto_erase(mic,16);
    if (!kck || !frame || length < 99 || length > 65539 ||
        (frame[0] != 1 && frame[0] != 2) || frame[1] != 3 ||
        ((unsigned)frame[2] << 8 | frame[3]) != length - 4 || frame[4] != 2 ||
        !(frame[5] & 1) || (frame[6] & 7) != 2 || ((unsigned)frame[97] << 8 | frame[98]) != length - 99) return -1;
    uint8_t digest[WC_SHA_DIGEST_SIZE], zeros[16] = {0};
    Hmac hmac;
    int result = wc_HmacInit(&hmac,0,INVALID_DEVID);
    if (result) goto done;
    result = wc_HmacSetKey(&hmac,WC_SHA,kck,16);
    if (!result) result = wc_HmacUpdate(&hmac,frame,81);
    if (!result) result = wc_HmacUpdate(&hmac,zeros,16);
    if (!result) result = wc_HmacUpdate(&hmac,frame + 97,length - 97);
    if (!result) result = wc_HmacFinal(&hmac,digest);
    if (!result) wifi_crypto_memcpy(mic,digest,16);
    wc_HmacFree(&hmac);
done:
    wifi_crypto_erase(&hmac,sizeof(hmac)); wifi_crypto_erase(digest,sizeof(digest));
    return result ? -1 : 0;
}
int wifi_wpa2_eapol_verify(const uint8_t kck[16], const uint8_t* frame, unsigned length) {
    uint8_t mic[16];
    if (wifi_wpa2_eapol_mic(kck,frame,length,mic)) return 0;
    unsigned difference = 0;
    for (unsigned i = 0; i < 16; i++) difference |= mic[i] ^ frame[81 + i];
    wifi_crypto_erase(mic,sizeof(mic));
    return difference == 0;
}
