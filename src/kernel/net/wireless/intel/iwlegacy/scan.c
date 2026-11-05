/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright(c) 2003 - 2011 Intel Corporation. All rights reserved.
 * Passive scan command layout and RX notification framing adapted from
 * Linux iwlegacy/3945-mac.c, 3945.c and commands.h.
 */
#include "internal.h"
#include "scan.h"
#define SCAN_HEADER 216u /* fixed header 28 + TX command 52 + four SSID IEs 136 */
static void put16(uint8_t* p, unsigned value) { p[0] = value; p[1] = value >> 8; }
static void put32(uint8_t* p, uint32_t value) {
    p[0] = value; p[1] = value >> 8; p[2] = value >> 16; p[3] = value >> 24;
}
static int equal(const uint8_t* a, const uint8_t* b, unsigned size) {
    for (unsigned i = 0; i < size; i++) if (a[i] != b[i]) return 0;
    return 1;
}
int il3945_scan_build(const il3945_eeprom_t* eeprom, unsigned band,
                       uint8_t* out, unsigned capacity, unsigned* length) {
    if (length) *length = 0;
    if (!eeprom || !out || !length || capacity < SCAN_HEADER || (band != 2 && band != 5)) return IL_INVALID;
    unsigned count = 0;
    for (unsigned i = 0; i < IL3945_CHANNEL_COUNT; i++)
        if ((eeprom->channels[i].flags & 1) && eeprom->channels[i].band == band) count++;
    if (!count) return IL_INVALID;
    unsigned size = SCAN_HEADER + count * 8;
    if (size > capacity) return IL_INVALID;
    for (unsigned i = 0; i < size; i++) out[i] = 0;
    put16(out,size); out[3] = count; put16(out + 8,0xFFFF); /* never promote to active */
    put32(out + 20,band == 2 ? 5u : 0u); /* band 2.4 / auto detect */
    put32(out + 24,4u | 8u | 16u | 64u); /* group, no decrypt, beacon aware */
    unsigned offset = SCAN_HEADER;
    for (unsigned i = 0; i < IL3945_CHANNEL_COUNT; i++) {
        const il3945_channel_t* ch = &eeprom->channels[i];
        if (!(ch->flags & 1) || ch->band != band) continue;
        out[offset + 1] = ch->number;
        /* Type 0, gain 0, TX command 0: passive reception only. */
        put16(out + offset + 4,10);
        put16(out + offset + 6,band == 2 ? 120 : 110);
        offset += 8;
    }
    *length = size;
    return IL_OK;
}
static int suite(const uint8_t* p, unsigned type) {
    return p[0] == 0 && p[1] == 0x0F && p[2] == 0xAC && p[3] == type;
}
static il_security_t rsn_security(const uint8_t* p, unsigned length) {
    if (length < 8 || il_le16(p) != 1 || !suite(p + 2,4)) return IL_SECURITY_UNSUPPORTED;
    unsigned count = il_le16(p + 6); p += 8; length -= 8;
    if (!count || count > length / 4) return IL_SECURITY_UNSUPPORTED;
    unsigned ccmp = 0;
    for (unsigned i = 0; i < count; i++) ccmp |= suite(p + i * 4,4);
    p += count * 4; length -= count * 4;
    if (length < 2) return IL_SECURITY_UNSUPPORTED;
    count = il_le16(p); p += 2; length -= 2;
    if (!count || count > length / 4) return IL_SECURITY_UNSUPPORTED;
    unsigned psk = 0;
    for (unsigned i = 0; i < count; i++) psk |= suite(p + i * 4,2);
    p += count * 4; length -= count * 4;
    if (length == 1 || (length >= 2 && (il_le16(p) & 0x40))) return IL_SECURITY_UNSUPPORTED;
    if (length >= 2) { p += 2; length -= 2; } /* RSN capabilities */
    if (length) {
        if (length < 2) return IL_SECURITY_UNSUPPORTED;
        count = il_le16(p); p += 2; length -= 2;
        if (count > length / 16) return IL_SECURITY_UNSUPPORTED;
        p += count * 16; length -= count * 16;
        if (length != 0 && (length != 4 || !suite(p,6))) return IL_SECURITY_UNSUPPORTED;
    }
    return ccmp && psk ? IL_SECURITY_WPA2_PSK : IL_SECURITY_UNSUPPORTED;
}
static void beacon(il_scan_results_t* results, const uint8_t* frame, unsigned length,
                     unsigned channel, unsigned band, int signal) {
    if (length < 36 || (frame[0] != 0x80 && frame[0] != 0x50) || (frame[1] & 0xC7) || (il_le16(frame + 22) & 15) ||
        (frame[16] & 1)) { results->malformed++; return; }
    unsigned any = 0; for (unsigned i = 0; i < 6; i++) any |= frame[16 + i];
    if (!any) { results->malformed++; return; }
    il_network_t found = {0};
    for (unsigned i = 0; i < 6; i++) found.bssid[i] = frame[16 + i];
    found.channel = channel; found.band = band; found.signal_dbm = signal;
    found.capability = il_le16(frame + 34); found.beacon_interval = il_le16(frame + 32);
    found.security = found.capability & 16 ? IL_SECURITY_UNSUPPORTED : IL_SECURITY_OPEN;
    unsigned ssid_seen = 0, rsn_seen = 0;
    for (unsigned offset = 36; offset < length;) {
        if (length - offset < 2 || frame[offset + 1] > length - offset - 2) {
            results->malformed++; return;
        }
        unsigned tag = frame[offset], size = frame[offset + 1];
        const uint8_t* data = frame + offset + 2;
        if (tag == 0) {
            if (ssid_seen++ || size > 32) { results->malformed++; return; }
            found.ssid_length = size;
            for (unsigned i = 0; i < size; i++) found.ssid[i] = data[i];
        } else if (tag == 3 && (size != 1 || (band == 2 && data[0] != channel))) {
            results->malformed++; return;
        } else if (tag == 48) {
            if (rsn_seen++) { results->malformed++; return; }
            found.security = rsn_security(data,size);
        }
        offset += size + 2;
    }
    if (!ssid_seen || !(found.capability & 1) || (found.capability & 2)) return; /* infrastructure APs */
    unsigned index = results->count;
    for (unsigned i = 0; i < results->count; i++)
        if (equal(found.bssid,results->networks[i].bssid,6)) { index = i; break; }
    if (index == IL_SCAN_NETWORKS) {
        unsigned weakest = 0;
        for (unsigned i = 1; i < results->count; i++)
            if (results->networks[i].signal_dbm < results->networks[weakest].signal_dbm) weakest = i;
        results->dropped++;
        if (signal <= results->networks[weakest].signal_dbm) return;
        index = weakest;
    }
    if (index < results->count && !found.ssid_length && results->networks[index].ssid_length) {
        found.ssid_length = results->networks[index].ssid_length;
        for (unsigned i = 0; i < found.ssid_length; i++) found.ssid[i] = results->networks[index].ssid[i];
    }
    results->networks[index] = found;
    if (index == results->count) results->count++;
}
void il3945_scan_event(void* context, uint8_t command, uint8_t flags, uint16_t sequence,
                        const uint8_t* payload, unsigned length) {
    il_scan_results_t* r = context;
    (void)flags; (void)sequence;
    if (command == 2) { r->error = IL_VERIFY; return; }
    if (command == 0x84) {
        if (length != 11 || payload[0] != r->expected_channels) r->error = IL_VERIFY;
        else { r->complete = 1; r->completed_channels = payload[0]; }
        return;
    }
    if (command != 0x1B) return;
    /* stats(8) + variable PHY bytes + hdr(8) + MPDU + trailer(16). */
    if (length < 32 || payload[0] > 20 || length < 32u + payload[0]) { r->malformed++; return; }
    unsigned header = 8 + payload[0];
    unsigned frame_length = il_le16(payload + header + 6);
    if (frame_length > length - header - 8 - 16) { r->malformed++; return; }
    const uint8_t* frame = payload + header + 8;
    if ((il_le32(frame + frame_length) & 3) != 3) return;
    unsigned channel = il_le16(payload + header);
    unsigned band = il_le16(payload + header + 2) & 1 ? 2 : 5;
    if (band != r->active_band || channel >= 256 || !r->allowed_channels[channel]) return;
    beacon(r,frame,frame_length,channel,band,(int)payload[2] - 95);
}
int il3945_scan_run(const il_io_t* io, il_rx_queue_t* rx, il_command_queue_t* commands,
                     const il3945_eeprom_t* eeprom, il_scan_results_t* results) {
    if (!il_io_valid(io) || !rx || !commands || !eeprom || !results) return IL_INVALID;
    *results = (il_scan_results_t){0};
    uint8_t payload[SCAN_HEADER + IL3945_CHANNEL_COUNT * 8];
    for (unsigned band = 2; band <= 5; band += 3) {
        unsigned size;
        if (il3945_scan_build(eeprom,band,payload,sizeof(payload),&size)) continue;
        results->active_band = band; results->complete = 0; results->expected_channels = payload[3];
        for (unsigned i = 0; i < 256; i++) results->allowed_channels[i] = 0;
        for (unsigned i = 0; i < IL3945_CHANNEL_COUNT; i++) {
            const il3945_channel_t* ch = &eeprom->channels[i];
            if ((ch->flags & 1) && ch->band == band) results->allowed_channels[ch->number] = 1;
        }
        int result = il3945_command_send(io,rx,commands,0x80,payload,size,1000,il3945_scan_event,results);
        if (result) return result;
        if (commands->response_length != 4 || il_le32(commands->response) != 1) return IL_VERIFY;
        for (unsigned tick = 0; !results->complete && !results->error && tick < 100000; tick++) {
            uint32_t gp = io->read32(io->context,CSR_GP_CNTRL), irq = io->read32(io->context,CSR_INT);
            if (gp == 0xFFFFFFFFu || irq == 0xFFFFFFFFu) return IL_ACCESS;
            if (!(gp & CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW)) return IL_RFKILL;
            if (irq & (CSR_INT_BIT_HW_ERR | CSR_INT_BIT_SW_ERR)) return IL_VERIFY;
            if (irq) io->write32(io->context,CSR_INT,irq);
            result = il3945_rx_poll(io,rx,il3945_scan_event,results);
            if (result < 0) return result;
            io->delay_us(io->context,100);
        }
        if (results->error) return results->error;
        if (!results->complete) return IL_TIMEOUT;
    }
    return IL_OK;
}
