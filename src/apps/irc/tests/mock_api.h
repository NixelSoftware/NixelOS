#include <stdint.h>
#define TYPES_H
#include <string.h>
#include "../../../system/user/window_api.h"
enum { USER_TCP_CLOSED=0, USER_TCP_SYN_SENT=2, USER_TCP_ESTABLISHED=4, USER_TCP_CLOSE_WAIT=7 };
static uint32_t ticks;
static const char* rx;
static char sent[8192];
static int state = USER_TCP_ESTABLISHED;
static int32_t sys_tcp_close(uint32_t h) { (void)h; return 0; }
static int32_t sys_dns_start(const char* h) { (void)h; return 1; }
static int32_t sys_dns_poll(uint32_t h, uint32_t* ip) { (void)h; *ip=1; return 1; }
static int32_t sys_tcp_connect(uint32_t ip, uint16_t p) { (void)ip; (void)p; return 1; }
static int32_t sys_tcp_state(uint32_t h) { (void)h; return state; }
static int32_t sys_tcp_send(uint32_t h, const void* p, uint32_t n) {
    (void)h; unsigned len = strlen(sent); memcpy(sent+len,p,n); sent[len+n]=0; return 1;
}
static int32_t sys_tcp_read(uint32_t h, void* p, uint32_t n) {
    (void)h; if (!rx) return 0;
    unsigned size = strlen(rx); if (size > n) size = n;
    memcpy(p,rx,size); rx += size; return size;
}
static uint32_t sys_get_ticks(void) { return ticks; }
static int32_t sys_window_draw(uint32_t h, const user_window_draw_t* d) { (void)h; (void)d; return 0; }
static int32_t sys_window_present(uint32_t h) { (void)h; return 0; }
static int32_t sys_window_create(const user_window_desc_t* d) { (void)d; return 1; }
static int32_t sys_window_poll(uint32_t h, user_window_event_t* e) { (void)h; (void)e; return 0; }
static int32_t sys_window_close(uint32_t h) { (void)h; return 0; }
static void sys_yield(void) {}
static void sys_detach(void) {}
static void sys_write(const char* p, uint32_t n) { (void)p; (void)n; }
