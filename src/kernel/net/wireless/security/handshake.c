/* SPDX-License-Identifier: GPL-3.0-or-later
 * WPA2-PSK/CCMP four-way supplicant state over wolfCrypt key/MIC/AES helpers.
 */
#include "handshake.h"
#include "user_settings.h"
static const uint8_t own_rsn[] = {48,20,1,0,0,15,172,4,1,0,0,15,172,4,1,0,0,15,172,2,0,0};
static int equal(const uint8_t* a, const uint8_t* b, unsigned size) {
    unsigned difference = 0; for (unsigned i = 0; i < size; i++) difference |= a[i] ^ b[i];
    return difference == 0;
}
static unsigned le16(const uint8_t* p) { return p[0] | (unsigned)p[1] << 8; }
static unsigned be16(const uint8_t* p) { return (unsigned)p[0] << 8 | p[1]; }
static int counter_order(const uint8_t* a, const uint8_t* b) {
    for (unsigned i = 0; i < 8; i++) if (a[i] != b[i]) return a[i] > b[i] ? 1 : -1;
    return 0;
}
static int nonzero(const uint8_t* p, unsigned size) {
    unsigned any = 0; for (unsigned i = 0; i < size; i++) any |= p[i]; return any != 0;
}
static int suite(const uint8_t* p, unsigned type) {
    return p[0] == 0 && p[1] == 15 && p[2] == 172 && p[3] == type;
}
static int negotiated_rsn(const uint8_t* p, unsigned length) {
    if (!p || length < 20 || length > 257 || p[0] != 48 || p[1] != length - 2 ||
        le16(p + 2) != 1 || !suite(p + 4,4)) return 0;
    p += 8; length -= 8;
    unsigned count = le16(p); p += 2; length -= 2;
    if (!count || count > length / 4) return 0;
    unsigned ccmp = 0, psk = 0;
    for (unsigned i = 0; i < count; i++) ccmp |= suite(p + i * 4,4);
    p += count * 4; length -= count * 4;
    if (length < 2) return 0;
    count = le16(p); p += 2; length -= 2;
    if (!count || count > length / 4) return 0;
    for (unsigned i = 0; i < count; i++) psk |= suite(p + i * 4,2);
    p += count * 4; length -= count * 4;
    if (length == 1 || (length >= 2 && (le16(p) & 0x40))) return 0;
    if (length >= 2) { p += 2; length -= 2; }
    if (length) {
        if (length < 2) return 0;
        count = le16(p); p += 2; length -= 2;
        if (count > length / 16) return 0;
        p += count * 16; length -= count * 16;
        if (length && (length != 4 || !suite(p,6))) return 0;
    }
    return ccmp && psk;
}
void wifi_wpa2_station_clear(wifi_wpa2_station_t* s) { if (s) wifi_crypto_erase(s,sizeof(*s)); }
int wifi_wpa2_station_init(wifi_wpa2_station_t* s, const uint8_t pmk[32],
                             const uint8_t own[6], const uint8_t peer[6],
                             const uint8_t* ap_rsn, unsigned rsn_length) {
    if (!s || !pmk || !own || !peer || !nonzero(own,6) || !nonzero(peer,6) ||
        (own[0] & 1) || (peer[0] & 1) || equal(own,peer,6) || !negotiated_rsn(ap_rsn,rsn_length)) return -1;
    /* Caller clears/disconnects before reinitializing, preventing implicit
     * reinstall of an active key with freshly reset packet numbers. */
    if (s->initialized) return -1;
    wifi_wpa2_station_clear(s);
    wifi_crypto_memcpy(s->pmk,pmk,32); wifi_crypto_memcpy(s->own,own,6); wifi_crypto_memcpy(s->peer,peer,6);
    wifi_crypto_memcpy(s->ap_rsn,ap_rsn,rsn_length); s->ap_rsn_length = rsn_length; s->initialized = 1;
    return 0;
}
static void reply_header(uint8_t* out, unsigned size, uint8_t version, unsigned info,
                           const uint8_t replay[8]) {
    wifi_crypto_memset(out,0,size); out[0] = version; out[1] = 3;
    out[2] = (size - 4) >> 8; out[3] = size - 4; out[4] = 2;
    out[5] = info >> 8; out[6] = info;
    wifi_crypto_memcpy(out + 9,replay,8); out[97] = (size - 99) >> 8; out[98] = size - 99;
}
static int group_key(wifi_wpa2_station_t* s, const uint8_t* data, unsigned length,
                       uint8_t key[16], unsigned* id, unsigned need_rsn) {
    unsigned got_rsn = 0, got_gtk = 0;
    while (length) {
        if (data[0] == 221 && (length == 1 || data[1] == 0)) {
            for (unsigned i = 1; i < length; i++) if (data[i]) return -1;
            break;
        }
        if (length < 2 || data[1] > length - 2) return -1;
        unsigned size = data[1];
        if (data[0] == 48) {
            if (got_rsn++ || size + 2 != s->ap_rsn_length || !equal(data,s->ap_rsn,size + 2)) return -1;
        } else if (data[0] == 221 && size >= 4 && suite(data + 2,1)) {
            if (got_gtk++ || size != 22 || (data[6] & 0xF8) || data[7]) return -1;
            *id = data[6] & 3; wifi_crypto_memcpy(key,data + 8,16);
        }
        data += size + 2; length -= size + 2;
    }
    return (!need_rsn || got_rsn == 1) && got_gtk == 1 ? 0 : -1;
}
int wifi_wpa2_station_receive(wifi_wpa2_station_t* s, const uint8_t sender[6],
                                const uint8_t* frame, unsigned length, unsigned now_ms,
                                wifi_nonce_fn entropy, void* entropy_context,
                                uint8_t* response, unsigned capacity, unsigned* written) {
    if (written) *written = 0;
    if (!s || !s->initialized || !sender || !equal(sender,s->peer,6) || !frame || !response ||
        !written || length < 99 || length > WIFI_EAPOL_MAX || (frame[0] != 1 && frame[0] != 2) ||
        frame[1] != 3 || be16(frame + 2) != length - 4 || frame[4] != 2 ||
        be16(frame + 97) != length - 99 || be16(frame + 7) != 16) return -1;
    if (wifi_wpa2_station_expire(s,now_ms)) return -1;
    unsigned info = be16(frame + 5);
    if (info == 0x008A) { /* Message 1: pairwise, ACK, descriptor v2, no MIC. */
        if (!nonzero(frame + 17,32) || capacity < sizeof(s->m2)) return -1;
        if (s->phase && counter_order(frame + 9,s->replay) <= 0) {
            if (s->phase == 1 && counter_order(frame + 9,s->replay) == 0 && equal(frame + 17,s->anonce,32)) {
                wifi_crypto_memcpy(response,s->m2,sizeof(s->m2)); *written = sizeof(s->m2); return 1;
            }
            return -1;
        }
        uint8_t nonce[32], ptk[64], reply[121], mic[16];
        int result = !entropy || entropy(entropy_context,nonce,32) || !nonzero(nonce,32) ? -1 :
            wifi_wpa2_ptk(s->pmk,s->own,s->peer,nonce,frame + 17,ptk);
        if (!result) {
            reply_header(reply,sizeof(reply),frame[0],0x010A,frame + 9);
            wifi_crypto_memcpy(reply + 17,nonce,32); wifi_crypto_memcpy(reply + 99,own_rsn,sizeof(own_rsn));
            result = wifi_wpa2_eapol_mic(ptk,reply,sizeof(reply),mic);
            if (!result) wifi_crypto_memcpy(reply + 81,mic,16);
        }
        if (!result) {
            wifi_crypto_memcpy(s->m2,reply,sizeof(reply));
            wifi_crypto_memcpy(s->ptk,ptk,64); wifi_crypto_memcpy(s->snonce,nonce,32);
            wifi_crypto_memcpy(s->anonce,frame + 17,32); wifi_crypto_memcpy(s->replay,frame + 9,8);
            s->phase = 1; s->started_ms = now_ms;
            wifi_crypto_memcpy(response,s->m2,sizeof(s->m2)); *written = sizeof(s->m2);
        }
        wifi_crypto_erase(nonce,sizeof(nonce)); wifi_crypto_erase(ptk,sizeof(ptk));
        wifi_crypto_erase(reply,sizeof(reply)); wifi_crypto_erase(mic,sizeof(mic));
        return result ? -1 : 1;
    }
    if ((info != 0x13CA && info != 0x1382) || capacity < sizeof(s->m4)) return -1;
    unsigned pairwise_message = info == 0x13CA;
    if (s->authorized && length == s->m3_length && equal(frame,s->m3,length)) {
        wifi_crypto_memcpy(response,s->m4,sizeof(s->m4)); *written = sizeof(s->m4); return 1;
    }
    if ((pairwise_message ? s->phase != 1 : s->phase != 2 || !s->authorized) ||
        counter_order(frame + 9,s->replay) <= 0 || (pairwise_message && !equal(frame + 17,s->anonce,32)) ||
        !wifi_wpa2_eapol_verify(s->ptk,frame,length) || frame[71] || frame[72]) return -1;
    uint8_t data[512], gtk[16], reply[99], mic[16]; unsigned plain_length = 0, key_id = 0;
    int result = wifi_wpa2_unwrap(s->ptk + 16,frame + 99,length - 99,data,sizeof(data),&plain_length);
    if (!result) result = group_key(s,data,plain_length,gtk,&key_id,pairwise_message);
    wifi_ccmp_key_t pairwise = s->pairwise, group = s->group;
    if (!result && pairwise_message) result = wifi_ccmp_install(&pairwise,s->ptk + 32,0,s->own,s->peer);
    if (!result) result = wifi_ccmp_install(&group,gtk,key_id,s->own,s->peer);
    if (!result) {
        uint64_t rsc = 0;
        for (unsigned i = 0; i < 6; i++) rsc |= (uint64_t)frame[65 + i] << (i * 8);
        for (unsigned i = 0; i < 17; i++) if (group.rx_pn[i] < rsc) group.rx_pn[i] = rsc;
        reply_header(reply,sizeof(reply),frame[0],pairwise_message ? 0x030A : 0x0302,frame + 9);
        result = wifi_wpa2_eapol_mic(s->ptk,reply,sizeof(reply),mic);
        if (!result) wifi_crypto_memcpy(reply + 81,mic,16);
    }
    if (!result) {
        wifi_crypto_memcpy(s->m4,reply,sizeof(reply));
        s->pairwise = pairwise; s->group = group;
        wifi_crypto_memcpy(s->m3,frame,length); s->m3_length = length;
        wifi_crypto_memcpy(s->replay,frame + 9,8); s->phase = 2; s->authorized = 1;
        wifi_crypto_memcpy(response,s->m4,sizeof(s->m4)); *written = sizeof(s->m4);
    }
    wifi_crypto_erase(data,sizeof(data)); wifi_crypto_erase(gtk,sizeof(gtk));
    wifi_crypto_erase(reply,sizeof(reply)); wifi_crypto_erase(mic,sizeof(mic));
    wifi_crypto_erase(&pairwise,sizeof(pairwise)); wifi_crypto_erase(&group,sizeof(group));
    return result ? -1 : 1;
}
int wifi_wpa2_station_expire(wifi_wpa2_station_t* s, unsigned now_ms) {
    if (!s || !s->initialized || s->phase != 1 || (unsigned)(now_ms - s->started_ms) < 5000) return 0;
    wifi_wpa2_station_clear(s); return 1;
}

int wifi_wpa2_station_authorized(const wifi_wpa2_station_t* s) {
    return s && s->initialized && s->authorized;
}
int wifi_wpa2_station_encrypt(wifi_wpa2_station_t* s, const uint8_t* frame, unsigned length,
                                uint8_t* out, unsigned capacity, unsigned* written) {
    if (written) *written = 0;
    if (!wifi_wpa2_station_authorized(s)) return -1;
    return wifi_ccmp_encrypt(&s->pairwise,frame,length,out,capacity,written);
}
int wifi_wpa2_station_decrypt(wifi_wpa2_station_t* s, const uint8_t* frame, unsigned length,
                                uint8_t* out, unsigned capacity, unsigned* written) {
    if (written) *written = 0;
    if (!wifi_wpa2_station_authorized(s) || !frame || length < 24) return -1;
    return wifi_ccmp_decrypt(frame[4] & 1 ? &s->group : &s->pairwise,frame,length,out,capacity,written);
}
