#include "ntp.h"
#include "dns.h"
#include "udp.h"
#include "../hal/net.h"
#include "../task.h"
#include "../mutex.h"
#include "../fs.h"

#define SECOND 1000000000ull
static struct { uint32_t magic; int enabled, offset; char server[128]; } config =
    {0x31504e54, 1, 0, "ntp.nixelos.org"};
static int loaded, state;
static udp_socket_t* socket;
static uint64_t deadline, next_sync;
static uint8_t token[8];
static const char* status = "Waiting for network";
static int leap(int y) { return y % 4 == 0 && (y % 100 != 0 || y % 400 == 0); }
static int days(int y, int m) {
    static const int d[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    return d[m-1] + (m == 2 && leap(y));
}
uint64_t time_to_epoch(const rtc_time_t* t) {
    uint32_t d = 0;
    for (int y = 1970; y < t->year; y++) d += 365 + leap(y);
    for (int m = 1; m < t->month; m++) d += days(t->year, m);
    return ((uint64_t)(d + t->day - 1) * 24 + t->hour) * 3600 + t->minute * 60 + t->second;
}
void time_from_epoch(uint64_t seconds, rtc_time_t* t) {
    uint32_t sec = (uint32_t)seconds;
    uint32_t d = sec / 86400;
    t->second = sec % 60; t->minute = sec / 60 % 60; t->hour = sec / 3600 % 24;
    int y = 1970, m = 1;
    while (d >= (uint32_t)(365 + leap(y))) { d -= 365 + leap(y); y++; }
    while (d >= (uint32_t)days(y,m)) { d -= days(y,m); m++; }
    t->year = y; t->month = m; t->day = d + 1;
}
/* Called during boot while filesystem operations still run as root. */
void time_init(void) {
    fs_stat_t st;
    if (!fs_stat("/etc/time.cfg", &st))
        fs_write_file("/etc/time.cfg", (const uint8_t*)&config, sizeof(config));
    fs_chmod("/etc/time.cfg", 0777);
}
static int save_config(void) {
    /* Repair an existing file when the caller owns it or is root. */
    fs_chmod("/etc/time.cfg", 0777);
    if (fs_write_file("/etc/time.cfg", (const uint8_t*)&config, sizeof(config)) != sizeof(config)) return -1;
    fs_stat_t st;
    return fs_stat("/etc/time.cfg", &st) && st.mode == 0777 ? 1 : -1;
}
int time_sync_enabled(void) { return config.enabled; }
const char* time_server(void) { return config.server; }
int time_zone_offset(void) { return config.offset; }
const char* time_sync_status(void) { return config.enabled ? status : "Automatic sync disabled"; }
static void cancel(void) {
    if (state == 1) dns_resolve_async_cancel();
    if (socket) udp_disconnect(socket);
    socket = 0; state = 0;
}
void time_sync_now(void) { MUTEX_GUARD(kernel_mutex); cancel(); next_sync = 0; }
int time_configure(int enabled, const char* server, int offset) {
    MUTEX_GUARD(kernel_mutex);
    int n = 0, label = 0;
    if (!server || offset < -720 || offset > 840) return 0;
    while (server[n]) {
        char c = server[n];
        if (n >= 127) return 0;
        if (c == '.') { if (!label || label > 63) return 0; label = 0; }
        else { if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-')) return 0; label++; }
        n++;
    }
    if (!label || label > 63) return 0;
    cancel();
    for (int i = 0; i <= n; i++) config.server[i] = server[i];
    config.enabled = !!enabled; config.offset = offset; next_sync = 0;
    return save_config();
}
int time_set_local(const rtc_time_t* t) {
    MUTEX_GUARD(kernel_mutex);
    if (!t || t->year < 1970 || t->year > 2099 || t->month < 1 || t->month > 12 || t->day < 1 || t->day > days(t->year,t->month) || t->hour > 23 || t->minute > 59 || t->second > 59) return 0;
    int64_t utc = (int64_t)time_to_epoch(t) - config.offset * 60;
    if (utc < 0) return 0;
    rtc_set_epoch(utc); return 1;
}
static void receive(udp_socket_t* s, uint32_t ip, uint16_t port, const uint8_t* p, uint16_t n) {
    if (state != 2 || s != socket || ip != udp_get_remote_ip(s) || port != 123 || n < 48 || (p[0] & 7) != 4 || ((p[0] >> 3) & 7) < 3 || ((p[0] >> 3) & 7) > 4 || (p[0] >> 6) == 3) return;
    for (int i = 0; i < 8; i++) if (p[24+i] != token[i]) return;
    if (!p[1] || p[1] > 15) { status = "Server refused synchronization"; state = 3; return; }
    uint64_t sec = ((uint32_t)p[40]<<24) | ((uint32_t)p[41]<<16) | ((uint32_t)p[42]<<8) | p[43];
    if (!sec && !p[44] && !p[45] && !p[46] && !p[47]) return;
    /* NTP era rollover in 2036; supported wall dates are 1970..2099. */
    if (sec < 2208988800ull) sec += 4294967296ull;
    sec -= 2208988800ull;
    if (sec > 4102444799ull) return;
    rtc_set_epoch(sec); status = "Time synchronized"; state = 3;
}
void ntp_poll(void) {
    MUTEX_GUARD(kernel_mutex);
    if (!loaded) {
        loaded = 1;
        typeof(config) saved;
        if (fs_read_file("/etc/time.cfg", (uint8_t*)&saved, sizeof(saved)) == sizeof(saved) && saved.magic == config.magic && saved.server[127] == 0 && saved.offset >= -720 && saved.offset <= 840 && (saved.enabled == 0 || saved.enabled == 1)) config = saved;
    }
    uint64_t now = scheduler_clock();
    if (!config.enabled) return;
    if (!sys_net_is_ready() || !sys_net_get_ip_address()) { cancel(); status = "Waiting for network"; return; }
    if (state == 3) { cancel(); next_sync = now + 3600 * SECOND; return; }
    if (state && now >= deadline) { cancel(); status = "Sync failed; retrying in one minute"; next_sync = now + 60 * SECOND; }
    if (!state) {
        if (now < next_sync) return;
        if (!dns_resolve_async_start(config.server)) { next_sync = now + SECOND; return; }
        state = 1; deadline = now + 15 * SECOND; status = "Resolving time server...";
    } else if (state == 1) {
        uint32_t ip; int result = dns_resolve_async_poll(&ip);
        if (!result) return;
        state = 0;
        if (result < 0 || !(socket = udp_connect(ip,123))) { status = "Time server lookup failed"; next_sync = now + 60 * SECOND; return; }
        uint8_t packet[48] = {0}; packet[0] = 0x23;
        for (int i = 0; i < 8; i++) packet[40+i] = token[i] = (uint8_t)(now >> (i*8));
        udp_bind(socket,receive); state = 2; deadline = now + 5 * SECOND;
        status = "Synchronizing...";
        if (!udp_send(socket, packet, sizeof(packet))) { cancel(); status = "Could not reach time server"; next_sync = now + 60 * SECOND; }
    }
}
