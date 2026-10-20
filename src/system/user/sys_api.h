#ifndef SYSTEM_USER_SYS_API_H
#define SYSTEM_USER_SYS_API_H

#include "../../kernel/types.h"
#include "../../kernel/fs.h"
#include "../../kernel/drivers/net.h"
#include "window_api.h"
#include "memory_api.h"

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
    SYS_YIELD = 9,           /* services kernel event loops, then returns to the app */
    SYS_WINDOW_CREATE = 10,  /* EBX: readable user_window_desc_t*; returns window handle */
    SYS_WINDOW_DRAW = 11,    /* EBX: handle, ECX: readable user_window_draw_t* */
    SYS_WINDOW_POLL = 12,    /* EBX: handle, ECX: writable user_window_event_t* */
    SYS_WINDOW_PRESENT = 13, /* EBX: handle; blits the window area */
    SYS_WINDOW_CLOSE = 14,   /* EBX: handle */
    SYS_FS_READ = 15,        /* EBX: filename, ECX: writable buffer or null, EDX: capacity (max 16 KiB; zero queries size) */
    SYS_FS_LIST = 16,        /* EBX: writable [FS_MAX_FILES][FS_MAX_FILENAME] output */
    SYS_FLOPPY_INIT = 17,    /* initializes the floppy controller */
    SYS_FLOPPY_READ = 18,    /* EBX: LBA, ECX: sector count (1..18), EDX: writable data */
    SYS_FLOPPY_TEST_WRITE = 19, /* writes the built-in test pattern to reserved LBA 2879 */
    SYS_FS_WRITE = 20,       /* EBX: filename, ECX: readable buffer, EDX: length (max 64 KiB) */
    SYS_TCP_CONNECT = 21,    /* EBX: remote IPv4, ECX: remote port; returns owned socket handle */
    SYS_TCP_STATE = 22,      /* EBX: socket handle; returns USER_TCP_* state */
    SYS_TCP_SEND = 23,       /* EBX: handle, ECX: readable data, EDX: length (max 1448) */
    SYS_TCP_READ = 24,       /* EBX: handle, ECX: writable output, EDX: capacity */
    SYS_TCP_CLOSE = 25,      /* EBX: socket handle */
    SYS_DNS_START = 26,      /* EBX: hostname; returns resolver handle */
    SYS_DNS_POLL = 27,       /* EBX: resolver handle, ECX: writable IPv4 output */
    SYS_MEMORY_INFO = 28     /* EBX: writable user_memory_info_t; 0 success, -1 invalid */
};

enum {
    USER_TCP_CLOSED = 0,
    USER_TCP_SYN_SENT = 2,
    USER_TCP_ESTABLISHED = 4,
    USER_TCP_FIN_WAIT1 = 5,
    USER_TCP_FIN_WAIT2 = 6,
    USER_TCP_CLOSE_WAIT = 7
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

static inline int32_t sys_memory_info(user_memory_info_t* out) {
    int32_t result;
    __asm__ volatile ("int $0x80"
                      : "=a"(result)
                      : "0"((uint32_t)SYS_MEMORY_INFO), "b"(out)
                      : "ecx", "edx", "memory", "cc");
    return result;
}

static inline int32_t sys_fs_read(const char* filename, void* buffer,
                                  uint32_t capacity) {
    int32_t result;
    __asm__ volatile ("int $0x80"
                      : "=a"(result)
                      : "0"((uint32_t)SYS_FS_READ), "b"(filename),
                        "c"(buffer), "d"(capacity)
                      : "memory", "cc");
    return result;
}

static inline int32_t sys_fs_list(char files[FS_MAX_FILES][FS_MAX_FILENAME]) {
    int32_t result;
    __asm__ volatile ("int $0x80"
                      : "=a"(result)
                      : "0"((uint32_t)SYS_FS_LIST), "b"(files)
                      : "ecx", "edx", "memory", "cc");
    return result;
}

static inline int32_t sys_floppy_init(void) {
    int32_t result;
    __asm__ volatile ("int $0x80"
                      : "=a"(result)
                      : "0"((uint32_t)SYS_FLOPPY_INIT)
                      : "ebx", "ecx", "edx", "memory", "cc");
    return result;
}

static inline int32_t sys_floppy_read(uint32_t lba, uint32_t count,
                                     void* buffer) {
    int32_t result;
    __asm__ volatile ("int $0x80"
                      : "=a"(result)
                      : "0"((uint32_t)SYS_FLOPPY_READ), "b"(lba),
                        "c"(count), "d"(buffer)
                      : "memory", "cc");
    return result;
}

static inline int32_t sys_floppy_write_test_pattern(void) {
    int32_t result;
    __asm__ volatile ("int $0x80"
                      : "=a"(result)
                      : "0"((uint32_t)SYS_FLOPPY_TEST_WRITE)
                      : "ebx", "ecx", "edx", "memory", "cc");
    return result;
}

static inline int32_t sys_fs_write(const char* filename, const void* buffer,
                                   uint32_t length) {
    int32_t result;
    __asm__ volatile ("int $0x80"
                      : "=a"(result)
                      : "0"((uint32_t)SYS_FS_WRITE), "b"(filename),
                        "c"(buffer), "d"(length)
                      : "memory", "cc");
    return result;
}

static inline int32_t sys_tcp_connect(uint32_t remote_ip, uint16_t remote_port) {
    int32_t result;
    __asm__ volatile ("int $0x80"
                      : "=a"(result)
                      : "0"((uint32_t)SYS_TCP_CONNECT), "b"(remote_ip),
                        "c"((uint32_t)remote_port)
                      : "edx", "memory", "cc");
    return result;
}

static inline int32_t sys_tcp_state(uint32_t handle) {
    int32_t result;
    __asm__ volatile ("int $0x80"
                      : "=a"(result)
                      : "0"((uint32_t)SYS_TCP_STATE), "b"(handle)
                      : "ecx", "edx", "memory", "cc");
    return result;
}

static inline int32_t sys_tcp_send(uint32_t handle, const void* data,
                                   uint32_t length) {
    int32_t result;
    __asm__ volatile ("int $0x80"
                      : "=a"(result)
                      : "0"((uint32_t)SYS_TCP_SEND), "b"(handle),
                        "c"(data), "d"(length)
                      : "memory", "cc");
    return result;
}

static inline int32_t sys_tcp_read(uint32_t handle, void* output,
                                   uint32_t capacity) {
    int32_t result;
    __asm__ volatile ("int $0x80"
                      : "=a"(result)
                      : "0"((uint32_t)SYS_TCP_READ), "b"(handle),
                        "c"(output), "d"(capacity)
                      : "memory", "cc");
    return result;
}

static inline int32_t sys_tcp_close(uint32_t handle) {
    int32_t result;
    __asm__ volatile ("int $0x80"
                      : "=a"(result)
                      : "0"((uint32_t)SYS_TCP_CLOSE), "b"(handle)
                      : "ecx", "edx", "memory", "cc");
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

static inline int32_t sys_dns_start(const char* hostname) {
    int32_t result;
    __asm__ volatile ("int $0x80"
                      : "=a"(result)
                      : "0"((uint32_t)SYS_DNS_START), "b"(hostname)
                      : "ecx", "edx", "memory", "cc");
    return result;
}

static inline int32_t sys_dns_poll(uint32_t handle, uint32_t* address) {
    int32_t result;
    __asm__ volatile ("int $0x80"
                      : "=a"(result)
                      : "0"((uint32_t)SYS_DNS_POLL), "b"(handle), "c"(address)
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

static inline int32_t sys_window_create(const user_window_desc_t* description) {
    int32_t result;
    __asm__ volatile ("int $0x80"
                      : "=a"(result)
                      : "0"((uint32_t)SYS_WINDOW_CREATE), "b"(description)
                      : "ecx", "edx", "memory", "cc");
    return result;
}

static inline int32_t sys_window_draw(uint32_t handle,
                                      const user_window_draw_t* drawing) {
    int32_t result;
    __asm__ volatile ("int $0x80"
                      : "=a"(result)
                      : "0"((uint32_t)SYS_WINDOW_DRAW), "b"(handle), "c"(drawing)
                      : "edx", "memory", "cc");
    return result;
}

static inline int32_t sys_window_poll(uint32_t handle,
                                      user_window_event_t* event) {
    int32_t result;
    __asm__ volatile ("int $0x80"
                      : "=a"(result)
                      : "0"((uint32_t)SYS_WINDOW_POLL), "b"(handle), "c"(event)
                      : "edx", "memory", "cc");
    return result;
}

static inline int32_t sys_window_present(uint32_t handle) {
    int32_t result;
    __asm__ volatile ("int $0x80"
                      : "=a"(result)
                      : "0"((uint32_t)SYS_WINDOW_PRESENT), "b"(handle)
                      : "ecx", "edx", "memory", "cc");
    return result;
}

static inline int32_t sys_window_close(uint32_t handle) {
    int32_t result;
    __asm__ volatile ("int $0x80"
                      : "=a"(result)
                      : "0"((uint32_t)SYS_WINDOW_CLOSE), "b"(handle)
                      : "ecx", "edx", "memory", "cc");
    return result;
}

#endif
