#ifndef SYSTEM_USER_SYS_API_H
#define SYSTEM_USER_SYS_API_H

#include "../../kernel/types.h"
#include "../../kernel/drivers/net.h"

/* i386 ABI: call number in EAX, arguments in EBX/ECX/EDX, result in EAX. */
enum {
    SYS_WRITE = 1,     /* EBX: buffer, ECX: length; returns bytes written or -1 */
    SYS_GET_TICKS = 2, /* returns the timer tick count */
    SYS_EXIT = 3,      /* EBX: process status; does not return */
    SYS_SHUTDOWN = 4,  /* requests a kernel-managed system shutdown */
    SYS_NET_DIAGNOSTICS = 5, /* EBX: output snapshot; returns ready state */
    SYS_DNS_RESOLVE = 6,     /* EBX: hostname, ECX: writable IPv4 output */
    SYS_PING_START = 7,      /* EBX: destination IPv4, ECX: sequence; returns request handle */
    SYS_PING_POLL = 8,       /* EBX: handle, ECX: writable ms output; 0 pending, 1 reply, 2 timeout */
    SYS_YIELD = 9            /* services kernel event loops, then returns to the app */
};

static inline int32_t sys_write(const char* buffer, uint32_t length) {
    int32_t result;
    __asm__ volatile ("int $0x80"
                      : "=a"(result)
                      : "0"((uint32_t)SYS_WRITE), "b"(buffer), "c"(length)
                      : "edx", "memory", "cc");
    return result;
}

static inline uint32_t sys_get_ticks(void) {
    uint32_t result;
    __asm__ volatile ("int $0x80"
                      : "=a"(result)
                      : "0"((uint32_t)SYS_GET_TICKS)
                      : "ebx", "ecx", "edx", "memory", "cc");
    return result;
}

static inline void sys_exit(uint32_t status) {
    __asm__ volatile ("int $0x80"
                      :
                      : "a"((uint32_t)SYS_EXIT), "b"(status)
                      : "ecx", "edx", "memory", "cc");
    for (;;) { }
}

static inline void sys_shutdown(void) {
    __asm__ volatile ("int $0x80"
                      :
                      : "a"((uint32_t)SYS_SHUTDOWN)
                      : "ebx", "ecx", "edx", "memory", "cc");
    for (;;) { }
}

static inline int32_t sys_net_diagnostics(net_diagnostics_t* diagnostics) {
    int32_t result;
    __asm__ volatile ("int $0x80"
                      : "=a"(result)
                      : "0"((uint32_t)SYS_NET_DIAGNOSTICS), "b"(diagnostics)
                      : "ecx", "edx", "memory", "cc");
    return result;
}

static inline int32_t sys_dns_resolve(const char* hostname, uint32_t* address) {
    int32_t result;
    __asm__ volatile ("int $0x80"
                      : "=a"(result)
                      : "0"((uint32_t)SYS_DNS_RESOLVE), "b"(hostname), "c"(address)
                      : "edx", "memory", "cc");
    return result;
}

static inline int32_t sys_ping_start(uint32_t destination, uint16_t sequence) {
    int32_t result;
    __asm__ volatile ("int $0x80"
                      : "=a"(result)
                      : "0"((uint32_t)SYS_PING_START), "b"(destination),
                        "c"((uint32_t)sequence)
                      : "memory", "cc");
    return result;
}

static inline int32_t sys_ping_poll(uint32_t handle, uint32_t* milliseconds) {
    int32_t result;
    __asm__ volatile ("int $0x80"
                      : "=a"(result)
                      : "0"((uint32_t)SYS_PING_POLL), "b"(handle), "c"(milliseconds)
                      : "edx", "memory", "cc");
    return result;
}

static inline void sys_yield(void) {
    __asm__ volatile ("int $0x80"
                      :
                      : "a"((uint32_t)SYS_YIELD)
                      : "ebx", "ecx", "edx", "memory", "cc");
}

#endif
