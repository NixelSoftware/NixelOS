#ifndef SYSTEM_USER_SYS_API_H
#define SYSTEM_USER_SYS_API_H

#include "../../kernel/types.h"
#include "../../kernel/fs.h"
#include "../../kernel/hal/net_types.h"
#include "window_api.h"
#include "memory_api.h"
#include "audio_api.h"
#include "arch_syscall.h"

/* Sensitive calls return this value without performing the operation when
 * the calling task is not root. Other failures retain their existing -1 ABI. */
#define SYS_ERR_PERMISSION (-2)

/* i386 ABI: call number in EAX, arguments in EBX/ECX/EDX, result in EAX. */
enum {
    SYS_WRITE = 1,     /* EBX: buffer, ECX: length; returns bytes written or -1 */
    SYS_GET_TICKS = 2, /* returns the timer tick count */
    SYS_EXIT = 3,      /* EBX: process status; does not return */
    SYS_SHUTDOWN = 4,  /* UID 0: shutdown; SYS_ERR_PERMISSION otherwise */
    SYS_NET_DIAGNOSTICS = 5, /* EBX: output snapshot; returns ready state */
    SYS_DNS_RESOLVE = 6,     /* EBX: hostname, ECX: writable IPv4 output */
    SYS_PING_START = 7,      /* EBX: destination IPv4, ECX: sequence; returns request handle */
    SYS_PING_POLL = 8,       /* EBX: handle, ECX: writable ms output; 0 pending, 1 reply, 2 timeout */
    SYS_YIELD = 9,           /* sleeps until the next timer tick; other tasks keep running */
    SYS_WINDOW_CREATE = 10,  /* EBX: readable user_window_desc_t*; returns window handle */
    SYS_WINDOW_DRAW = 11,    /* EBX: handle, ECX: readable user_window_draw_t* */
    SYS_WINDOW_POLL = 12,    /* EBX: handle, ECX: writable user_window_event_t* */
    SYS_WINDOW_PRESENT = 13, /* EBX: handle; blits the window area */
    SYS_WINDOW_CLOSE = 14,   /* EBX: handle */
    SYS_FS_READ = 15,        /* EBX: filename, ECX: writable buffer or null, EDX: capacity (max 16 KiB; zero queries size) */
    SYS_FS_LIST = 16,        /* EBX: writable [FS_MAX_FILES][FS_MAX_FILENAME] output */
    SYS_FLOPPY_INIT = 17,    /* UID 0: initializes the raw floppy controller */
    SYS_FLOPPY_READ = 18,    /* UID 0: EBX LBA, ECX count (1..18), EDX writable data */
    SYS_FLOPPY_TEST_WRITE = 19, /* UID 0: writes the test pattern to reserved LBA 2879 */
    SYS_FS_WRITE = 20,       /* EBX: filename, ECX: readable buffer, EDX: length (max 64 KiB) */
    SYS_TCP_CONNECT = 21,    /* EBX: remote IPv4, ECX: remote port; returns owned socket handle */
    SYS_TCP_STATE = 22,      /* EBX: socket handle; returns USER_TCP_* state */
    SYS_TCP_SEND = 23,       /* EBX: handle, ECX: data, EDX: length <=1448; 1 queued, 0 retry/error */
    SYS_TCP_READ = 24,       /* EBX: handle, ECX: writable output, EDX: capacity */
    SYS_TCP_CLOSE = 25,      /* EBX: socket handle */
    SYS_DNS_START = 26,      /* EBX: hostname; returns resolver handle */
    SYS_DNS_POLL = 27,       /* EBX: resolver handle, ECX: writable IPv4 output */
    SYS_MEMORY_INFO = 28, /* EBX: writable user_memory_info_t */
    SYS_AUDIO_INIT = 29, /* EBX: source rate; open caller mixed signed 16-bit stereo stream */
    SYS_AUDIO_WRITE = 30, /* EBX: PCM, ECX: bytes (1..16384, whole source frames); 0 queue full, positive accepted */
    SYS_AUDIO_STATUS = 31, /* 1 playing, 0 idle, -1 unavailable */
    SYS_AUDIO_SHUTDOWN = 32, /* closes only the caller audio stream */
    SYS_FS_READ_AT = 33, /* as FS_READ, with byte offset in ESI */
    SYS_AUDIO_DRAIN = 34, /* finish all queued samples; rejects further writes */
    SYS_REBOOT = 35, /* UID 0: reboot; SYS_ERR_PERMISSION otherwise */
    SYS_DETACH = 36, /* reparent current process to PID 0; releases terminal wait */
    SYS_SERIAL_LOG_READ = 44, /* UID 0: EBX output, ECX capacity (1..256), EDX cursor */
    SYS_AUDIO_DEVICE_NAME = 43, /* EBX: output string, ECX: capacity (1..64); selected HAL device */
    SYS_AUDIO_OPL = 42, /* legacy hardware OPL returns -1; synthesize PCM into the shared mixer */
    SYS_GETPID = 37,
    SYS_GETPPID = 38,
    SYS_FS_LIST_DIR = 39, /* EBX: path, ECX: names, EDX: FS type bits */
    SYS_FILE_PICK_RESULT = 41, /* EBX: request token, ECX: selected path (empty cancels) */
    SYS_APP_LAUNCH = 40, /* EBX: executable path, ECX: arguments; starts detached */
    SYS_TCP_LISTEN = 45, /* EBX: local port; ports below 1024 require UID 0 */
    SYS_TCP_ACCEPT = 46, /* EBX: listener handle; returns new connected socket handle, 0 if pending, -1 on err */
    SYS_TCP_REMOTE_IP = 47, /* EBX: socket handle; returns uint32_t IPv4 address */
    SYS_RTC_TIME = 48, /* EBX: writable user_rtc_time_t* */
    SYS_AUDIO_CONFIGURE = 50, /* EBX: user_audio_format_t; open caller-owned mixed PCM stream */
    SYS_FS_GETCWD = 51, /* EBX: output path, ECX: capacity */
    SYS_FS_HOME = 52, /* EBX: output home path, ECX: capacity */
    SYS_FS_MKDIR = 53, /* EBX: directory path; 0 success, -1 failure */
    SYS_FS_DELETE = 54, /* EBX: path; filesystem permissions apply */
    SYS_PCI_DEVICE_INFO = 49, /* EBX: index, ECX: writable user_pci_device_t* */
    SYS_TASK_LIST = 55,   /* EBX: writable user_task_info_t*, ECX: capacity */
    SYS_UTC_SECONDS = 57, /* signed UTC Unix seconds; -1 outside the supported range */
    SYS_MEMORY_ALLOC = 58, /* EBX: bytes; returns zero-filled page-aligned pointer or null */
    SYS_MEMORY_FREE = 59, /* EBX: original allocation base; returns 0 or -1 */
    SYS_FS_CHMOD = 60, /* EBX path, ECX mode (0777); owner or root only */
    SYS_TASK_KILL = 56    /* EBX: PID to kill */
};

/* Allocations are isolated per app and reclaimed automatically on app teardown.
 * Sizes round up to 4 KiB. Free requires the returned base, not an interior pointer.
 * These mappings can be passed to all syscalls that accept user buffers. */
static inline void* sys_memory_alloc(uint32_t bytes) {
    return (void*)arch_syscall((uint32_t)(SYS_MEMORY_ALLOC), (uint32_t)(bytes), 0, 0, 0);
}
static inline int32_t sys_memory_free(void* allocation) {
    return (int32_t)arch_syscall((uint32_t)(SYS_MEMORY_FREE), (uint32_t)(allocation), 0, 0, 0);
}

static inline int32_t sys_fs_getcwd(char* out, uint32_t capacity) {
    return (int32_t)arch_syscall((uint32_t)(SYS_FS_GETCWD), (uint32_t)(out), (uint32_t)(capacity), 0, 0);
}
static inline int32_t sys_fs_home(char* out, uint32_t capacity) {
    return (int32_t)arch_syscall((uint32_t)(SYS_FS_HOME), (uint32_t)(out), (uint32_t)(capacity), 0, 0);
}
static inline int32_t sys_fs_chmod(const char* path, uint32_t mode) {
    return (int32_t)arch_syscall((uint32_t)((uint32_t)SYS_FS_CHMOD), (uint32_t)(path), (uint32_t)(mode), 0, 0);
}

static inline int32_t sys_fs_delete(const char* path) {
    return (int32_t)arch_syscall((uint32_t)(SYS_FS_DELETE), (uint32_t)(path), 0, 0, 0);
}
static inline int32_t sys_fs_mkdir(const char* path) {
    return (int32_t)arch_syscall((uint32_t)(SYS_FS_MKDIR), (uint32_t)(path), 0, 0, 0);
}

typedef struct { uint32_t next, lost; } user_serial_cursor_t;
static inline int32_t sys_serial_log_read(char* output, uint32_t capacity, user_serial_cursor_t* cursor) {
    return (int32_t)arch_syscall((uint32_t)(SYS_SERIAL_LOG_READ), (uint32_t)(output), (uint32_t)(capacity), (uint32_t)(cursor), 0);
}

static inline int32_t sys_file_pick_result(uint32_t token, const char* path) {
    return (int32_t)arch_syscall((uint32_t)(SYS_FILE_PICK_RESULT), (uint32_t)(token), (uint32_t)(path), 0, 0);
}

enum {
    USER_TCP_CLOSED = 0,
    USER_TCP_SYN_SENT = 2,
    USER_TCP_ESTABLISHED = 4,
    USER_TCP_FIN_WAIT1 = 5,
    USER_TCP_FIN_WAIT2 = 6,
    USER_TCP_CLOSE_WAIT = 7
};

static inline int32_t sys_detach(void) {
    return (int32_t)arch_syscall((uint32_t)(SYS_DETACH), 0, 0, 0, 0);
}
static inline int32_t sys_getpid(void) {
    return (int32_t)arch_syscall((uint32_t)(SYS_GETPID), 0, 0, 0, 0);
}
static inline int32_t sys_getppid(void) {
    return (int32_t)arch_syscall((uint32_t)(SYS_GETPPID), 0, 0, 0, 0);
}
static inline int32_t sys_audio_device_name(char* name, uint32_t capacity) {
    return (int32_t)arch_syscall((uint32_t)(SYS_AUDIO_DEVICE_NAME), (uint32_t)(name), (uint32_t)(capacity), 0, 0);
}
static inline int32_t sys_audio_opl(uint32_t reg, uint32_t value) {
    return (int32_t)arch_syscall((uint32_t)(SYS_AUDIO_OPL), (uint32_t)(reg), (uint32_t)(value), 0, 0);
}
static inline int32_t sys_audio_configure(const user_audio_format_t* format) {
    return (int32_t)arch_syscall((uint32_t)(SYS_AUDIO_CONFIGURE), (uint32_t)(format), 0, 0, 0);
}
static inline int32_t sys_audio_init(uint32_t rate) {
    return (int32_t)arch_syscall((uint32_t)(SYS_AUDIO_INIT), (uint32_t)(rate), 0, 0, 0);
}
static inline int32_t sys_audio_write(const void* data, uint32_t length) {
    return (int32_t)arch_syscall((uint32_t)(SYS_AUDIO_WRITE), (uint32_t)(data), (uint32_t)(length), 0, 0);
}
static inline int32_t sys_audio_drain(void) {
    return (int32_t)arch_syscall((uint32_t)(SYS_AUDIO_DRAIN), 0, 0, 0, 0);
}
static inline int32_t sys_audio_status(void) {
    return (int32_t)arch_syscall((uint32_t)(SYS_AUDIO_STATUS), 0, 0, 0, 0);
}
static inline void sys_audio_shutdown(void) {
    (void)arch_syscall((uint32_t)(SYS_AUDIO_SHUTDOWN), 0, 0, 0, 0);
}
static inline int32_t sys_fs_read_at(const char* path, void* data,
                                   uint32_t capacity, uint32_t offset) {
    return (int32_t)arch_syscall((uint32_t)(SYS_FS_READ_AT), (uint32_t)(path), (uint32_t)(data), (uint32_t)(capacity), (uint32_t)(offset));
}

static inline int32_t sys_write(const char* buffer, uint32_t length) {
    return (int32_t)arch_syscall((uint32_t)((uint32_t)SYS_WRITE), (uint32_t)(buffer), (uint32_t)(length), 0, 0);
}

static inline uint32_t sys_get_ticks(void) {
    return arch_syscall((uint32_t)((uint32_t)SYS_GET_TICKS), 0, 0, 0, 0);
}

static inline int32_t sys_memory_info(user_memory_info_t* out) {
    return (int32_t)arch_syscall((uint32_t)((uint32_t)SYS_MEMORY_INFO), (uint32_t)(out), 0, 0, 0);
}

static inline int32_t sys_fs_read(const char* filename, void* buffer,
                                  uint32_t capacity) {
    return (int32_t)arch_syscall((uint32_t)((uint32_t)SYS_FS_READ), (uint32_t)(filename), (uint32_t)(buffer), (uint32_t)(capacity), 0);
}

static inline int32_t sys_fs_list_dir(const char* path,
        char names[FS_MAX_FILES][FS_MAX_FILENAME], uint8_t types[FS_MAX_FILES]) {
    return (int32_t)arch_syscall((uint32_t)(SYS_FS_LIST_DIR), (uint32_t)(path), (uint32_t)(names), (uint32_t)(types), 0);
}
static inline int32_t sys_app_launch(const char* path, const char* arguments) {
    return (int32_t)arch_syscall((uint32_t)(SYS_APP_LAUNCH), (uint32_t)(path), (uint32_t)(arguments), 0, 0);
}

static inline int32_t sys_fs_list(char files[FS_MAX_FILES][FS_MAX_FILENAME]) {
    return (int32_t)arch_syscall((uint32_t)((uint32_t)SYS_FS_LIST), (uint32_t)(files), 0, 0, 0);
}

static inline int32_t sys_floppy_init(void) {
    return (int32_t)arch_syscall((uint32_t)((uint32_t)SYS_FLOPPY_INIT), 0, 0, 0, 0);
}

static inline int32_t sys_floppy_read(uint32_t lba, uint32_t count,
                                     void* buffer) {
    return (int32_t)arch_syscall((uint32_t)((uint32_t)SYS_FLOPPY_READ), (uint32_t)(lba), (uint32_t)(count), (uint32_t)(buffer), 0);
}

static inline int32_t sys_floppy_write_test_pattern(void) {
    return (int32_t)arch_syscall((uint32_t)((uint32_t)SYS_FLOPPY_TEST_WRITE), 0, 0, 0, 0);
}

static inline int32_t sys_fs_write(const char* filename, const void* buffer,
                                   uint32_t length) {
    return (int32_t)arch_syscall((uint32_t)((uint32_t)SYS_FS_WRITE), (uint32_t)(filename), (uint32_t)(buffer), (uint32_t)(length), 0);
}

static inline int32_t sys_tcp_connect(uint32_t remote_ip, uint16_t remote_port) {
    return (int32_t)arch_syscall((uint32_t)((uint32_t)SYS_TCP_CONNECT), (uint32_t)(remote_ip), (uint32_t)((uint32_t)remote_port), 0, 0);
}

static inline int32_t sys_tcp_state(uint32_t handle) {
    return (int32_t)arch_syscall((uint32_t)((uint32_t)SYS_TCP_STATE), (uint32_t)(handle), 0, 0, 0);
}

static inline int32_t sys_tcp_send(uint32_t handle, const void* data,
                                   uint32_t length) {
    return (int32_t)arch_syscall((uint32_t)((uint32_t)SYS_TCP_SEND), (uint32_t)(handle), (uint32_t)(data), (uint32_t)(length), 0);
}

static inline int32_t sys_tcp_read(uint32_t handle, void* output,
                                   uint32_t capacity) {
    return (int32_t)arch_syscall((uint32_t)((uint32_t)SYS_TCP_READ), (uint32_t)(handle), (uint32_t)(output), (uint32_t)(capacity), 0);
}

static inline int32_t sys_tcp_close(uint32_t handle) {
    return (int32_t)arch_syscall((uint32_t)((uint32_t)SYS_TCP_CLOSE), (uint32_t)(handle), 0, 0, 0);
}

static inline void sys_exit(uint32_t status) {
    (void)arch_syscall((uint32_t)((uint32_t)SYS_EXIT), (uint32_t)(status), 0, 0, 0);
        for (;;) { }
}

static inline int32_t sys_shutdown(void) {
    return (int32_t)arch_syscall((uint32_t)((uint32_t)SYS_SHUTDOWN), 0, 0, 0, 0);
}

static inline int32_t sys_reboot(void) {
    return (int32_t)arch_syscall((uint32_t)((uint32_t)SYS_REBOOT), 0, 0, 0, 0);
}

static inline int32_t sys_net_diagnostics(net_diagnostics_t* diagnostics) {
    return (int32_t)arch_syscall((uint32_t)((uint32_t)SYS_NET_DIAGNOSTICS), (uint32_t)(diagnostics), 0, 0, 0);
}

static inline int32_t sys_dns_resolve(const char* hostname, uint32_t* address) {
    return (int32_t)arch_syscall((uint32_t)((uint32_t)SYS_DNS_RESOLVE), (uint32_t)(hostname), (uint32_t)(address), 0, 0);
}

static inline int32_t sys_dns_start(const char* hostname) {
    return (int32_t)arch_syscall((uint32_t)((uint32_t)SYS_DNS_START), (uint32_t)(hostname), 0, 0, 0);
}

static inline int32_t sys_dns_poll(uint32_t handle, uint32_t* address) {
    return (int32_t)arch_syscall((uint32_t)((uint32_t)SYS_DNS_POLL), (uint32_t)(handle), (uint32_t)(address), 0, 0);
}

static inline int32_t sys_ping_start(uint32_t destination, uint16_t sequence) {
    return (int32_t)arch_syscall((uint32_t)((uint32_t)SYS_PING_START), (uint32_t)(destination), (uint32_t)((uint32_t)sequence), 0, 0);
}

static inline int32_t sys_ping_poll(uint32_t handle, uint32_t* milliseconds) {
    return (int32_t)arch_syscall((uint32_t)((uint32_t)SYS_PING_POLL), (uint32_t)(handle), (uint32_t)(milliseconds), 0, 0);
}

static inline void sys_yield(void) {
    (void)arch_syscall((uint32_t)((uint32_t)SYS_YIELD), 0, 0, 0, 0);
}

static inline int32_t sys_window_create(const user_window_desc_t* description) {
    return (int32_t)arch_syscall((uint32_t)((uint32_t)SYS_WINDOW_CREATE), (uint32_t)(description), 0, 0, 0);
}

static inline int32_t sys_window_draw(uint32_t handle,
                                      const user_window_draw_t* drawing) {
    return (int32_t)arch_syscall((uint32_t)((uint32_t)SYS_WINDOW_DRAW), (uint32_t)(handle), (uint32_t)(drawing), 0, 0);
}

static inline int32_t sys_window_poll(uint32_t handle,
                                      user_window_event_t* event) {
    return (int32_t)arch_syscall((uint32_t)((uint32_t)SYS_WINDOW_POLL), (uint32_t)(handle), (uint32_t)(event), 0, 0);
}

static inline int32_t sys_window_present(uint32_t handle) {
    return (int32_t)arch_syscall((uint32_t)((uint32_t)SYS_WINDOW_PRESENT), (uint32_t)(handle), 0, 0, 0);
}

static inline int32_t sys_window_close(uint32_t handle) {
    return (int32_t)arch_syscall((uint32_t)((uint32_t)SYS_WINDOW_CLOSE), (uint32_t)(handle), 0, 0, 0);
}

static inline int32_t sys_tcp_listen(uint16_t local_port) {
    return (int32_t)arch_syscall((uint32_t)((uint32_t)SYS_TCP_LISTEN), (uint32_t)((uint32_t)local_port), 0, 0, 0);
}

static inline int32_t sys_tcp_accept(uint32_t listener_handle) {
    return (int32_t)arch_syscall((uint32_t)((uint32_t)SYS_TCP_ACCEPT), (uint32_t)(listener_handle), 0, 0, 0);
}

static inline uint32_t sys_tcp_remote_ip(uint32_t handle) {
    return arch_syscall((uint32_t)((uint32_t)SYS_TCP_REMOTE_IP), (uint32_t)(handle), 0, 0, 0);
}

typedef struct {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint16_t year;
} user_rtc_time_t;

static inline int32_t sys_utc_seconds(void) {
    return (int32_t)arch_syscall((uint32_t)((uint32_t)SYS_UTC_SECONDS), 0, 0, 0, 0);
}

static inline int32_t sys_rtc_time(user_rtc_time_t* out) {
    return (int32_t)arch_syscall((uint32_t)((uint32_t)SYS_RTC_TIME), (uint32_t)(out), 0, 0, 0);
}

typedef struct {
    uint8_t bus, slot, function, header_type;
    uint16_t vendor_id, device_id;
    uint8_t class_code, subclass, prog_if, revision;
    uint8_t irq_line, irq_pin;
} user_pci_device_t;

static inline int32_t sys_pci_device_info(uint32_t index, user_pci_device_t* out) {
    return (int32_t)arch_syscall((uint32_t)((uint32_t)SYS_PCI_DEVICE_INFO), (uint32_t)(index), (uint32_t)(out), 0, 0);
}

typedef struct {
    uint32_t pid;
    uint32_t ppid;
    uint32_t state;
    char name[16];
} user_task_info_t;

static inline int32_t sys_task_list(user_task_info_t* out, uint32_t capacity) {
    return (int32_t)arch_syscall((uint32_t)(SYS_TASK_LIST), (uint32_t)(out), (uint32_t)(capacity), 0, 0);
}

static inline int32_t sys_task_kill(uint32_t pid) {
    return (int32_t)arch_syscall((uint32_t)(SYS_TASK_KILL), (uint32_t)(pid), 0, 0, 0);
}

#endif
