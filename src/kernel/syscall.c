#include "../system/services/audio/mixer.h"
#include "../system/services/audio/system_sound.h"
#include "hal/audio.h"
#include "syscall.h"
#include "users.h"
#include "syscall_policy.h"
#include "task.h"
#include "mutex.h"
#include "arch/arch.h"
#include "mm.h"
#include "drivers/serial.h"
#include "drivers/power.h"
#include "hal/net_types.h"
#include "drivers/pci.h"
#include "drivers/floppy.h"
#include "drivers/rtc.h"
#include "net/tcp.h"
#include "fs.h"
#include "../system/loader/app_loader.h"
#include "hal/net.h"
#include "../system/graphics/wm/desktop.h"
#include "net/dns.h"
#include "net/icmp.h"
#include "../system/graphics/wm/wm.h"

#define SYS_WRITE_MAX 256u
//#define SYS_FS_READ_MAX 16384u
#define SYS_FS_READ_MAX 2097152u
#define FLOPPY_SECTOR_COUNT 2880u
#define FLOPPY_SECTOR_SIZE 512u
#define USER_TCP_SOCKET_MAX 16u
#define USER_TCP_QUEUE_SIZE 131072u
#define USER_TCP_BACKLOG    8u
//#define SYS_FS_WRITE_MAX 65536u
#define SYS_FS_WRITE_MAX 2097152u
#define SYS_TCP_SEND_MAX TCP_MAX_PAYLOAD
#define SYS_TCP_READ_MAX 4096u
#define SYS_WINDOW_IMAGE_MAX 32768u
_Static_assert(PNG_TARGET_W == USER_WINDOW_IMAGE_TARGET_W && PNG_TARGET_H == USER_WINDOW_IMAGE_TARGET_H, "native image ABI dimensions");
#define USER_PING_REQUEST_MAX 16u
#define USER_PING_TIMEOUT_TICKS 200u
#define USER_PING_STALL_POLLS 100000u
extern volatile uint32_t frame_timer;
extern void kernel_user_write(const char* text, uint32_t length);

typedef struct {
    uint32_t owner_directory;
    uint32_t handle;
    uint32_t destination;
    uint32_t started_tick;
    uint32_t reply_tick;
    uint32_t last_tick;
    uint32_t stalled_polls;
    uint16_t identifier;
    uint16_t sequence;
    uint8_t active;
    uint8_t completed;
    uint8_t timed_out;
} user_ping_request_t;  

static user_ping_request_t user_ping_requests[USER_PING_REQUEST_MAX];
static uint32_t next_ping_handle = 1;
static uint16_t next_ping_identifier = 1;

typedef struct {
    uint32_t owner_directory;
    uint32_t handle;
    tcp_socket_t* socket;
    uint32_t read_head;
    uint32_t read_count;
    uint8_t active;
    uint8_t is_listener;
    uint32_t accepted_handles[USER_TCP_BACKLOG];
    uint8_t accept_head;
    uint8_t accept_count;
    uint8_t data[USER_TCP_QUEUE_SIZE];
} user_tcp_socket_t;

static user_tcp_socket_t user_tcp_sockets[USER_TCP_SOCKET_MAX];
static uint32_t next_tcp_handle = 1;
static uint8_t user_dns_active;
static uint32_t user_dns_owner;
static uint32_t user_dns_handle;
static uint32_t next_dns_handle = 1;
static void user_tcp_discard_pending(user_tcp_socket_t* listener);

static void user_tcp_reap_closed(uint32_t owner_dir) {
    for (uint32_t i = 0; i < USER_TCP_SOCKET_MAX; i++) {
        if (user_tcp_sockets[i].active &&
            user_tcp_sockets[i].owner_directory == owner_dir &&
            user_tcp_sockets[i].socket) {
            
            tcp_socket_state_t st = tcp_get_state(user_tcp_sockets[i].socket);
            if (st == TCP_CLOSED && !user_tcp_sockets[i].read_count) {
                if (user_tcp_sockets[i].is_listener)
                    user_tcp_discard_pending(&user_tcp_sockets[i]);
                tcp_release(user_tcp_sockets[i].socket);
                user_tcp_sockets[i].socket = 0;
                user_tcp_sockets[i].active = 0;
                user_tcp_sockets[i].read_head = 0;
                user_tcp_sockets[i].read_count = 0;
            }
        }
    }
}

static user_tcp_socket_t* user_tcp_by_handle(uint32_t handle,
                                              uint32_t owner_directory) {
    for (uint32_t i = 0; i < USER_TCP_SOCKET_MAX; i++) {
        user_tcp_socket_t* slot = &user_tcp_sockets[i];
        if (slot->active && slot->handle == handle &&
            slot->owner_directory == owner_directory) return slot;
    }
    return 0;
}

static uint8_t user_tcp_receive(tcp_socket_t* socket, const uint8_t* data,
                                uint16_t length) {
    user_tcp_socket_t* slot = 0;
    for (uint32_t i = 0; i < USER_TCP_SOCKET_MAX; i++) {
        if (user_tcp_sockets[i].active &&
            user_tcp_sockets[i].socket == socket) {
            slot = &user_tcp_sockets[i];
            break;
        }
    }
    if (!slot || length > USER_TCP_QUEUE_SIZE - slot->read_count) return 0;
    for (uint32_t i = 0; i < length; i++) {
        uint32_t tail = (slot->read_head + slot->read_count) % USER_TCP_QUEUE_SIZE;
        slot->data[tail] = data[i];
        slot->read_count++;
    }
    return 1;
}

/* Closing a listener must release completed connections that were never
 * returned by accept. Already accepted sockets have independent handles. */
static void user_tcp_discard_pending(user_tcp_socket_t* listener) {
    while (listener->accept_count) {
        uint32_t handle = listener->accepted_handles[listener->accept_head];
        listener->accept_head = (listener->accept_head + 1u) % USER_TCP_BACKLOG;
        listener->accept_count--;
        user_tcp_socket_t* child = user_tcp_by_handle(handle, listener->owner_directory);
        if (!child || child == listener) continue;
        tcp_release(child->socket);
        child->socket = 0;
        child->active = 0;
        child->read_head = child->read_count = 0;
    }
}

static void syscall_ping_reply(uint32_t source_ip, uint16_t identifier,
                               uint16_t sequence, const uint8_t* payload,
                               uint16_t length) {
    (void)payload;
    (void)length;
    for (uint32_t i = 0; i < USER_PING_REQUEST_MAX; i++) {
        user_ping_request_t* request = &user_ping_requests[i];
        if (request->active && source_ip == request->destination &&
            identifier == request->identifier && sequence == request->sequence) {
            request->reply_tick = frame_timer;
            request->completed = 1;
        }
    }
}

static user_ping_request_t* ping_request_by_handle(uint32_t handle,
                                                    uint32_t owner_directory) {
    for (uint32_t i = 0; i < USER_PING_REQUEST_MAX; i++) {
        user_ping_request_t* request = &user_ping_requests[i];
        if (request->active && request->handle == handle &&
            request->owner_directory == owner_directory) return request;
    }
    return 0;
}

static uint8_t ping_identifier_in_use(uint16_t identifier) {
    for (uint32_t i = 0; i < USER_PING_REQUEST_MAX; i++) {
        if (user_ping_requests[i].active &&
            user_ping_requests[i].identifier == identifier) return 1;
    }
    return 0;
}

void syscall_forget_address_space(uint32_t directory_address) {
    if (!directory_address) return;
    audio_mixer_close(directory_address);
    for (uint32_t i = 0; i < USER_PING_REQUEST_MAX; i++) {
        if (user_ping_requests[i].owner_directory == directory_address)
            user_ping_requests[i].active = 0;
    }
    for (uint32_t i = 0; i < USER_TCP_SOCKET_MAX; i++) {
        user_tcp_socket_t* slot = &user_tcp_sockets[i];
        if (slot->active && slot->owner_directory == directory_address) {
            tcp_release(slot->socket);
            slot->active = 0;
        }
    }
    if (user_dns_active && user_dns_owner == directory_address) {
        dns_resolve_async_cancel();
        user_dns_active = 0;
    }
}

static int user_readable(uint32_t address, uint32_t length) {
    if (!length || !((address >= APP_VIRTUAL_BASE && address < APP_VIRTUAL_LIMIT &&
        length <= APP_VIRTUAL_LIMIT - address) ||
        (address >= APP_HEAP_BASE && address < APP_HEAP_LIMIT &&
        length <= APP_HEAP_LIMIT - address))) return 0;

    uint32_t* directory = (uint32_t*)arch_current_address_space();
    uint32_t end = address + length - 1u;
    for (uint32_t page = address & ~(PAGE_SIZE - 1u);;
         page += PAGE_SIZE) {
        uint32_t pde = directory[page >> 22];
        if ((pde & 0x5u) != 0x5u) return 0; /* present + user */
        uint32_t* table = (uint32_t*)(pde & 0xFFFFF000u);
        uint32_t pte = table[(page >> 12) & 0x3FFu];
        if ((pte & 0x5u) != 0x5u) return 0;
        if (page >= (end & ~(PAGE_SIZE - 1u))) break;
    }
    return 1;
}

static int user_writable(uint32_t address, uint32_t length) {
    if (!length || !((address >= APP_VIRTUAL_BASE && address < APP_VIRTUAL_LIMIT &&
        length <= APP_VIRTUAL_LIMIT - address) ||
        (address >= APP_HEAP_BASE && address < APP_HEAP_LIMIT &&
        length <= APP_HEAP_LIMIT - address))) return 0;

    uint32_t* directory = (uint32_t*)arch_current_address_space();
    uint32_t end = address + length - 1u;
    for (uint32_t page = address & ~(PAGE_SIZE - 1u);;
         page += PAGE_SIZE) {
        uint32_t pde = directory[page >> 22];
        if ((pde & 0x7u) != 0x7u) return 0;
        uint32_t* table = (uint32_t*)(pde & 0xFFFFF000u);
        uint32_t pte = table[(page >> 12) & 0x3FFu];
        if ((pte & 0x7u) != 0x7u) return 0;
        if (page >= (end & ~(PAGE_SIZE - 1u))) break;
    }
    return 1;
}

static int copy_user_text(uint32_t address, char* out, uint32_t capacity) {
    for (uint32_t i = 0; i < capacity; i++) {
        if (address+i < address || !user_readable(address+i, 1)) return 0;
        out[i] = *(const char*)(address+i);
        if (!out[i]) return 1;
    }
    return 0;
}

static void syscall_dispatch_impl(arch_syscall_t* sc) {
    if (!sc) return;
    if (!sc->from_user) {
        sc->result = (uint32_t)-1;
        return;
    }
    switch (sc->number) {
        case SYS_WRITE: {
            uint32_t address = sc->arg[0];
            uint32_t length = sc->arg[1];
            if (length > SYS_WRITE_MAX || !user_readable(address, length)) {
                sc->result = (uint32_t)-1;
                return;
            }
            const char* text = (const char*)address;
            if (!current_task->detached) kernel_user_write(text, length);
            sc->result = length;
            return;
        }
        case SYS_GET_TICKS:
            sc->result = frame_timer;
            return;
        case SYS_PCI_DEVICE_INFO: {
            uint32_t index = sc->arg[0];
            uint32_t out_addr = sc->arg[1];
            if (!user_writable(out_addr, sizeof(user_pci_device_t))) {
                sc->result = (uint32_t)-1;
                return;
            }
            if (index >= pci_device_count()) {
                sc->result = (uint32_t)-1; /* End of inventory */
                return;
            }
            const pci_device_t* dev = pci_device_at(index);
            if (!dev) {
                sc->result = (uint32_t)-1;
                return;
            }
            user_pci_device_t info = {
                .bus = dev->bus,
                .slot = dev->slot,
                .function = dev->function,
                .header_type = dev->header_type,
                .vendor_id = dev->vendor_id,
                .device_id = dev->device_id,
                .class_code = dev->class_code,
                .subclass = dev->subclass,
                .prog_if = dev->prog_if,
                .revision = dev->revision,
                .irq_line = dev->irq_line,
                .irq_pin = dev->irq_pin
            };
            *(user_pci_device_t*)out_addr = info;
            sc->result = 0;
            return;
        }
        case SYS_MEMORY_ALLOC:
            sc->result = paging_user_alloc(sc->arg[0]);
            return;
        case SYS_MEMORY_FREE:
            sc->result = (uint32_t)paging_user_free(sc->arg[0]);
            return;
        case SYS_MEMORY_INFO: {
            if (!user_writable(sc->arg[0], sizeof(user_memory_info_t))) {
                sc->result = (uint32_t)-1;
                return;
            }
            user_memory_info_t snapshot;
            paging_memory_info(&snapshot);
            *(user_memory_info_t*)sc->arg[0] = snapshot;
            sc->result = 0;
            return;
        }
        case SYS_DNS_START: {
            char hostname[254];
            uint32_t address = sc->arg[0];
            uint32_t length = 0;
            if (user_dns_active) {
                sc->result = (uint32_t)-1;
                return;
            }
            while (length < sizeof(hostname) - 1u) {
                uint32_t current = address + length;
                if (current < address || !user_readable(current, 1)) {
                    sc->result = (uint32_t)-1;
                    return;
                }
                hostname[length] = *(const char*)current;
                if (!hostname[length]) break;
                length++;
            }
            if (!length || length == sizeof(hostname) - 1u ||
                !dns_resolve_async_start(hostname)) {
                sc->result = (uint32_t)-1;
                return;
            }
            uint32_t handle = next_dns_handle++;
            if (!handle || handle > 0x7FFFFFFFu) {
                next_dns_handle = 2;
                handle = 1;
            }
            user_dns_active = 1;
            user_dns_owner = arch_current_address_space();
            user_dns_handle = handle;
            sc->result = handle;
            return;
        }
        case SYS_DNS_POLL: {
            uint32_t output_address = sc->arg[1];
            uint32_t resolved = 0;
            if (!user_dns_active || user_dns_handle != sc->arg[0] ||
                user_dns_owner != arch_current_address_space() ||
                !user_writable(output_address, sizeof(resolved))) {
                sc->result = (uint32_t)-1;
                return;
            }
            int result = dns_resolve_async_poll(&resolved);
            if (result == 1) *(uint32_t*)output_address = resolved;
            if (result != 0) user_dns_active = 0;
            sc->result = result == 1 ? 1u : result == 0 ? 0u : 2u;
            return;
        }
        case SYS_TASK_LIST: {
            task_info_t* buf = (task_info_t*)sc->arg[0];
            uint32_t cap = sc->arg[1];
            if (!buf || cap > 64 || !user_writable((uint32_t)buf, cap * sizeof(task_info_t))) {
                sc->result = (uint32_t)-1;
                break;
            }
            sc->result = (uint32_t)task_snapshot(buf, (int)cap);
            break;
        }
        case SYS_TASK_KILL: {
            uint32_t pid = sc->arg[0];
            if (pid == 0) {
                sc->result = (uint32_t)-1;
                break;
            }
            sc->result = (uint32_t)task_kill(pid);
            break;
        }
        case SYS_SERIAL_LOG_READ: {
            if (!sc->arg[1] || sc->arg[1] > 256 || !user_writable(sc->arg[0], sc->arg[1]) ||
                !user_writable(sc->arg[2], sizeof(user_serial_cursor_t))) {
                sc->result = (uint32_t)-1;
                return;
            }
            user_serial_cursor_t cursor = *(user_serial_cursor_t*)sc->arg[2];
            sc->result = serial_log_read((char*)sc->arg[0], sc->arg[1], &cursor.next, &cursor.lost);
            *(user_serial_cursor_t*)sc->arg[2] = cursor;
            return;
        }
        case SYS_AUDIO_DEVICE_NAME: {
            uint32_t capacity = sc->arg[1];
            if (!capacity || capacity > 64 || !user_writable(sc->arg[0], capacity)) {
                sc->result = (uint32_t)-1;
                return;
            }
            const char* name = audio_device_name(audio_device_selected());
            char* output = (char*)sc->arg[0];
            uint32_t n = 0;
            while (n + 1 < capacity && name[n]) { output[n] = name[n]; ++n; }
            output[n] = 0;
            sc->result = n;
            return;
        }
        case SYS_AUDIO_OPL:
            sc->result = audio_mixer_opl(arch_current_address_space(), sc->arg[0], sc->arg[1]);
            return;
        case SYS_AUDIO_CONFIGURE: {
            if (!user_readable(sc->arg[0], sizeof(user_audio_format_t))) {
                sc->result = (uint32_t)-1; return;
            }
            user_audio_format_t format = *(const user_audio_format_t*)sc->arg[0];
            sc->result = audio_mixer_open(arch_current_address_space(), &format);
            return;
        }
        case SYS_AUDIO_INIT: {
            user_audio_format_t format = {sc->arg[0], 2, USER_AUDIO_S16_LE};
            sc->result = audio_mixer_open(arch_current_address_space(), &format);
            return;
        }
        case SYS_AUDIO_WRITE:
            sc->result = (uint32_t)((!sc->arg[1] || sc->arg[1] > AUDIO_BUFFER_CHUNK_SIZE ||
                !user_readable(sc->arg[0], sc->arg[1])) ? -1 :
                audio_mixer_write(arch_current_address_space(), (const void*)sc->arg[0], sc->arg[1]));
            return;
        case SYS_AUDIO_DRAIN:
            sc->result = audio_mixer_drain(arch_current_address_space());
            return;
        case SYS_AUDIO_STATUS:
            sc->result = audio_mixer_status(arch_current_address_space());
            return;
        case SYS_AUDIO_SHUTDOWN:
            audio_mixer_close(arch_current_address_space());
            sc->result = 0;
            return;
        case SYS_FS_GETCWD:
        case SYS_FS_HOME: {
            const char* path = sc->number == SYS_FS_HOME ? user_home() : fs_getcwd();
            uint32_t length = 0; while (path[length]) length++;
            if (sc->arg[1] <= length || !user_writable(sc->arg[0], length + 1)) {
                sc->result = (uint32_t)-1; return;
            }
            for (uint32_t i = 0; i <= length; i++) ((char*)sc->arg[0])[i] = path[i];
            sc->result = length; return;
        }
        case SYS_FS_CHMOD: {
            char path[FS_MAX_PATH];
            sc->result = sc->arg[1] <= 0777 && copy_user_text(sc->arg[0], path, sizeof(path)) &&
                fs_chmod(path, (uint16_t)sc->arg[1]) ? 0u : (uint32_t)-1;
            return;
        }
        case SYS_FS_DELETE: {
            char path[FS_MAX_PATH];
            sc->result = copy_user_text(sc->arg[0], path, sizeof(path)) ?
                (fs_delete_file(path) ? 0u : (uint32_t)-1) : (uint32_t)-1;
            return;
        }
        case SYS_FS_MKDIR: {
            char path[FS_MAX_PATH];
            sc->result = copy_user_text(sc->arg[0], path, sizeof(path)) ?
                (uint32_t)fs_mkdir(path) : (uint32_t)-1;
            return;
        }
        case SYS_FS_READ_AT:
        case SYS_FS_READ: {
            char filename[FS_MAX_PATH];
            uint32_t filename_address = sc->arg[0];
            uint32_t output_address = sc->arg[1];
            uint32_t capacity = sc->arg[2];
            uint32_t length = 0;
            if (capacity > SYS_FS_READ_MAX ||
                (capacity && !user_writable(output_address, capacity)) ||
                (!capacity && output_address)) {
                sc->result = (uint32_t)-1;
                return;
            }
            while (length < sizeof(filename) - 1u) {
                uint32_t current = filename_address + length;
                if (current < filename_address || !user_readable(current, 1)) {
                    sc->result = (uint32_t)-1;
                    return;
                }
                filename[length] = *(const char*)current;
                if (!filename[length]) break;
                length++;
            }
            if (!length || length == sizeof(filename) - 1u) {
                sc->result = (uint32_t)-1;
                return;
            }
            sc->result = (uint32_t)fs_read_file_at(filename,
                capacity ? (uint8_t*)output_address : 0, capacity,
                sc->number == SYS_FS_READ_AT ? sc->arg[3] : 0);
            return;
        }
        case SYS_FS_LIST_DIR: {
            char path[FS_MAX_PATH];
            if (!copy_user_text(sc->arg[0], path, sizeof(path)) || !path[0] ||
                !user_writable(sc->arg[1], FS_MAX_FILES*FS_MAX_FILENAME) ||
                !user_writable(sc->arg[2], FS_MAX_FILES) ||
                !fs_access(path, FS_ACCESS_READ | FS_ACCESS_EXEC) ||
                (!(path[0] == '/' && !path[1]) && !fs_is_directory(path))) {
                sc->result = (uint32_t)-1; return;
            }
            sc->result = fs_list_dir_types(path, (char (*)[FS_MAX_FILENAME])sc->arg[1], (uint8_t*)sc->arg[2]);
            return;
        }
        case SYS_FILE_PICK_RESULT: {
            char path[FS_MAX_PATH];
            if (!copy_user_text(sc->arg[1], path, sizeof(path))) {
                sc->result = (uint32_t)-1; return;
            }
            sc->result = desktop_picker_result(sc->arg[0], current_task->id, path);
            return;
        }
        case SYS_APP_LAUNCH: {
            char path[FS_MAX_PATH], arguments[256];
            if (!copy_user_text(sc->arg[0], path, sizeof(path)) || !path[0] ||
                !copy_user_text(sc->arg[1], arguments, sizeof(arguments))) {
                sc->result = (uint32_t)-1; return;
            }
            sc->result = app_loader_launch_detached(kernel_get_api(), path, arguments);
            return;
        }
        case SYS_FS_LIST: {
            uint32_t output_address = sc->arg[0];
            uint32_t output_size = FS_MAX_FILES * FS_MAX_FILENAME;
            if (!user_writable(output_address, output_size)) {
                sc->result = (uint32_t)-1;
                return;
            }
            sc->result = (uint32_t)fs_list(
                (char (*)[FS_MAX_FILENAME])output_address);
            return;
        }
        case SYS_FS_WRITE: {
            char filename[FS_MAX_PATH];
            uint32_t filename_address = sc->arg[0];
            uint32_t source_address = sc->arg[1];
            uint32_t length = sc->arg[2];
            uint32_t name_length = 0;
            if (length > SYS_FS_WRITE_MAX ||
                (length && !user_readable(source_address, length))) {
                sc->result = (uint32_t)-1;
                return;
            }
            while (name_length < sizeof(filename) - 1u) {
                uint32_t current = filename_address + name_length;
                if (current < filename_address || !user_readable(current, 1)) {
                    sc->result = (uint32_t)-1;
                    return;
                }
                filename[name_length] = *(const char*)current;
                if (!filename[name_length]) break;
                name_length++;
            }
            if (!name_length || name_length == sizeof(filename) - 1u) {
                sc->result = (uint32_t)-1;
                return;
            }
            sc->result = (uint32_t)fs_write_file(filename,
                (const uint8_t*)source_address, length);
            return;
        }
        case SYS_TCP_CONNECT: {
            uint32_t owner_directory = arch_current_address_space();
            user_tcp_socket_t* slot = 0;
            for (uint32_t i = 0; i < USER_TCP_SOCKET_MAX; i++) {
                if (!user_tcp_sockets[i].active) {
                    slot = &user_tcp_sockets[i];
                    break;
                }
            }
            if (!slot || !sc->arg[1] || sc->arg[1] > 65535u) {
                sc->result = (uint32_t)-1;
                return;
            }
            tcp_socket_t* socket = tcp_connect(sc->arg[0], (uint16_t)sc->arg[1]);
            if (!socket) {
                sc->result = (uint32_t)-1;
                return;
            }
            uint32_t handle = next_tcp_handle++;
            if (!handle || handle > 0x7FFFFFFFu) {
                next_tcp_handle = 2;
                handle = 1;
            }
            slot->owner_directory = owner_directory;
            slot->handle = handle;
            slot->socket = socket;
            slot->read_head = 0;
            slot->read_count = 0;
            slot->active = 1;
            tcp_bind(socket, user_tcp_receive);
            sc->result = handle;
            return;
        }
        case SYS_TCP_STATE: {
            user_tcp_socket_t* slot = user_tcp_by_handle(
                sc->arg[0], arch_current_address_space());
            sc->result = slot ? (uint32_t)tcp_get_state(slot->socket)
                             : (uint32_t)-1;
            return;
        }
        case SYS_TCP_REMOTE_IP: {
            user_tcp_socket_t* slot = user_tcp_by_handle(
                sc->arg[0], arch_current_address_space());
            sc->result = (slot && slot->socket) ? tcp_get_remote_ip(slot->socket) : 0;
            return;
        }
        case SYS_TCP_SEND: {
            user_tcp_socket_t* slot = user_tcp_by_handle(
                sc->arg[0], arch_current_address_space());
            uint32_t length = sc->arg[2];
            if (!slot || length > SYS_TCP_SEND_MAX ||
                (length && !user_readable(sc->arg[1], length))) {
                sc->result = 0;
                return;
            }
            sc->result = (uint32_t)tcp_send(slot->socket,
                length ? (const uint8_t*)sc->arg[1] : 0, (uint16_t)length);
            return;
        }
        case SYS_TCP_READ: {
            user_tcp_socket_t* slot = user_tcp_by_handle(
                sc->arg[0], arch_current_address_space());
            uint32_t capacity = sc->arg[2];
            if (!slot || !capacity || capacity > SYS_TCP_READ_MAX ||
                !user_writable(sc->arg[1], capacity)) {
                sc->result = (uint32_t)-1;
                return;
            }
            tcp_resume(slot->socket);
            uint32_t count = slot->read_count < capacity
                           ? slot->read_count : capacity;
            uint8_t* output = (uint8_t*)sc->arg[1];
            for (uint32_t i = 0; i < count; i++) {
                output[i] = slot->data[slot->read_head];
                slot->read_head = (slot->read_head + 1u) % USER_TCP_QUEUE_SIZE;
            }
            slot->read_count -= count;
            tcp_resume(slot->socket);
            sc->result = count;
            return;
        }
        case SYS_TCP_CLOSE: {
            user_tcp_socket_t* slot = user_tcp_by_handle(
                sc->arg[0], arch_current_address_space());
            if (slot) {
                if (slot->is_listener) user_tcp_discard_pending(slot);
                if (slot->socket) {
                    tcp_disconnect(slot->socket);
                    tcp_release(slot->socket);
                    slot->socket = 0;
                }
                slot->active = 0;
                slot->read_head = 0;
                slot->read_count = 0;
                sc->result = 0;
                return;
            }
            sc->result = (uint32_t)-1;
            return;
        }
        case SYS_TCP_LISTEN: {
            uint32_t owner_directory = arch_current_address_space();
            user_tcp_socket_t* slot = 0;
            for (uint32_t i = 0; i < USER_TCP_SOCKET_MAX; i++) {
                if (!user_tcp_sockets[i].active) {
                    slot = &user_tcp_sockets[i];
                    break;
                }
            }
            if (!slot || !sc->arg[0] || sc->arg[0] > 65535u) {
                sc->result = (uint32_t)-1;
                return;
            }
            tcp_socket_t* socket = tcp_listen((uint16_t)sc->arg[0]);
            if (!socket) {
                sc->result = (uint32_t)-1;
                return;
            }
            uint32_t handle = next_tcp_handle++;
            if (!handle || handle > 0x7FFFFFFFu) {
                next_tcp_handle = 2;
                handle = 1;
            }
            slot->owner_directory = owner_directory;
            slot->handle = handle;
            slot->socket = socket;
            slot->read_head = 0;
            slot->read_count = 0;
            slot->active = 1;
            slot->is_listener = 1;
            slot->accept_head = 0;
            slot->accept_count = 0;
            sc->result = handle;
            return;
        }
        case SYS_TCP_ACCEPT: {
            uint32_t owner = arch_current_address_space();
            user_tcp_reap_closed(owner);
            user_tcp_socket_t* slot = user_tcp_by_handle(
                sc->arg[0], arch_current_address_space());
            if (!slot || !slot->is_listener) {
                sc->result = (uint32_t)-1;
                return;
            }
            while (slot->accept_count) {
                uint32_t client_handle = slot->accepted_handles[slot->accept_head];
                slot->accept_head = (slot->accept_head + 1u) % USER_TCP_BACKLOG;
                slot->accept_count--;
                if (user_tcp_by_handle(client_handle, owner)) {
                    sc->result = client_handle;
                    return;
                }
            }
            sc->result = 0;
            return;
        }
        case SYS_FLOPPY_INIT:
            sc->result = (uint32_t)floppy_init();
            return;
        case SYS_FLOPPY_READ: {
            uint32_t lba = sc->arg[0];
            uint32_t count = sc->arg[1];
            uint32_t buffer = sc->arg[2];
            if (!count || count > 18u || lba >= FLOPPY_SECTOR_COUNT ||
                count > FLOPPY_SECTOR_COUNT - lba ||
                !user_writable(buffer, count * FLOPPY_SECTOR_SIZE)) {
                sc->result = 0;
                return;
            }
            sc->result = (uint32_t)floppy_read_sectors(lba, (uint8_t)count,
                                                     (uint8_t*)buffer);
            return;
        }
        case SYS_FLOPPY_TEST_WRITE: {
            uint8_t pattern[FLOPPY_SECTOR_SIZE];
            for (uint32_t i = 0; i < FLOPPY_SECTOR_SIZE; i++)
                pattern[i] = (uint8_t)(i ^ 0xA5u);
            sc->result = (uint32_t)floppy_write_sectors(2879u, 1, pattern);
            return;
        }
        case SYS_YIELD:
            /* The desktop task owns service pumping. Park this waiter rather
             * than recursively entering WM callbacks on the app's stack. */
            task_sleep_ms(1);
            sc->result = 0;
            return;
        case SYS_WINDOW_CREATE: {
            user_window_desc_t description;
            uint32_t address = sc->arg[0];
            if (!user_readable(address, sizeof(description))) {
                sc->result = (uint32_t)-1;
                return;
            }
            description = *(const user_window_desc_t*)address;
            uint32_t title_length = 0;
            while (title_length < USER_WINDOW_TITLE_MAX && description.title[title_length])
                title_length++;
            if (title_length == USER_WINDOW_TITLE_MAX) {
                sc->result = (uint32_t)-1;
                return;
            }
            sc->result = (uint32_t)wm_user_window_create(
                &description, arch_current_address_space());
            return;
        }
        case SYS_WINDOW_DRAW: {
            user_window_draw_t drawing;
            user_3d_triangle_t triangles[USER_3D_MAX_TRIANGLES];
            char text[64];
            const char* text_pointer = 0;
            const uint8_t* image_pointer = 0;
            uint32_t address = sc->arg[1];
            if (!user_readable(address, sizeof(drawing))) {
                sc->result = (uint32_t)-1;
                return;
            }
            drawing = *(const user_window_draw_t*)address;
            if (drawing.operation == USER_WINDOW_DRAW_TEXT ||
                drawing.operation == USER_WINDOW_DRAW_BUTTON ||
                drawing.operation == USER_WINDOW_DRAW_TEXT_SCALED) {
                uint32_t length = 0;
                while (length < sizeof(text)) {
                    uint32_t current = (uint32_t)drawing.text + length;
                    if (current < (uint32_t)drawing.text || !user_readable(current, 1)) {
                        sc->result = (uint32_t)-1;
                        return;
                    }
                    text[length] = drawing.text[length];
                    if (!text[length]) break;
                    length++;
                }
                if (length == sizeof(text)) {
                    sc->result = (uint32_t)-1;
                    return;
                }
                text_pointer = text;
            } else if (drawing.operation == USER_WINDOW_DRAW_PIXELS ||
                       drawing.operation == USER_WINDOW_DRAW_PIXELS_SCALED) {
                if (drawing.width <= 0 || drawing.width > 640 ||
                    drawing.height <= 0 || drawing.height > 480 ||
                    drawing.data_length != (uint32_t)drawing.width * drawing.height * 4u ||
                    !user_readable((uint32_t)drawing.data, drawing.data_length)) {
                    sc->result = (uint32_t)-1;
                    return;
                }
                image_pointer = drawing.data;
            } else if (drawing.operation == USER_WINDOW_DRAW_TRIANGLES) {
                uint32_t source = (uint32_t)drawing.data;
                if (!drawing.data_length || drawing.data_length > sizeof(triangles) ||
                    drawing.data_length % sizeof(triangles[0]) ||
                    !user_readable(source, drawing.data_length)) {
                    sc->result = (uint32_t)-1;
                    return;
                }
                /* Snapshot user memory before entering the GPU/WM critical section. */
                const uint8_t* input = (const uint8_t*)source;
                uint8_t* output = (uint8_t*)triangles;
                for (uint32_t i = 0; i < drawing.data_length; i++) output[i] = input[i];
                image_pointer = (const uint8_t*)triangles;
            } else if (drawing.operation == USER_WINDOW_DRAW_IMAGE || drawing.operation == USER_WINDOW_DRAW_IMAGE_CLIPPED) {
                uint32_t image_address = (uint32_t)drawing.data;
                if (!drawing.data_length || drawing.data_length > SYS_WINDOW_IMAGE_MAX ||
                    !user_readable(image_address, drawing.data_length)) {
                    sc->result = (uint32_t)-1;
                    return;
                }
                image_pointer = (const uint8_t*)image_address;
            }
            sc->result = (uint32_t)wm_user_window_draw(
                sc->arg[0], arch_current_address_space(), &drawing, text_pointer,
                image_pointer);
            return;
        }
        case SYS_WINDOW_POLL: {
            uint32_t output_address = sc->arg[1];
            if (!user_writable(output_address, sizeof(user_window_event_t))) {
                sc->result = (uint32_t)-1;
                return;
            }
            sc->result = (uint32_t)wm_user_window_poll(
                sc->arg[0], arch_current_address_space(),
                (user_window_event_t*)output_address);
            return;
        }
        case SYS_WINDOW_PRESENT:
            sc->result = (uint32_t)wm_user_window_present(
                sc->arg[0], arch_current_address_space());
            return;
        case SYS_WINDOW_CLOSE:
            sc->result = (uint32_t)wm_user_window_close(
                sc->arg[0], arch_current_address_space());
            return;
        case SYS_DETACH:
            sc->result = task_detach();
            return;
        case SYS_GETPID:
            sc->result = current_task->id;
            return;
        case SYS_GETPPID:
            sc->result = task_parent_id();
            return;
        case SYS_EXIT:
            arch_user_exit_requested = 1;
            return;
        case SYS_SHUTDOWN:
            power_shutdown();
            sc->result = (uint32_t)-1;
            return;
        case SYS_REBOOT:
            power_reboot();
            sc->result = (uint32_t)-1;
            return;
        case SYS_UTC_SECONDS: {
            uint64_t now = rtc_get_epoch();
            sc->result = now <= 2147483647u ? (uint32_t)now : (uint32_t)-1;
            return;
        }
        case SYS_RTC_TIME: {
            if (!user_writable(sc->arg[0], sizeof(rtc_time_t))) {
                sc->result = (uint32_t)-1;
                return;
            }
            rtc_time_t t;
            rtc_get_time(&t);
            *(rtc_time_t*)sc->arg[0] = t;
            sc->result = 0;
            return;
        }
        case SYS_NET_DIAGNOSTICS: {
            uint32_t address = sc->arg[0];
            if (!user_writable(address, sizeof(net_diagnostics_t))) {
                sc->result = (uint32_t)-1;
                return;
            }
            sys_net_poll();
            sys_net_get_diagnostics((net_diagnostics_t*)address);
            sc->result = sys_net_is_ready() ? 1u : 0u;
            return;
        }
        case SYS_DNS_RESOLVE: {
            uint32_t hostname_address = sc->arg[0];
            uint32_t output_address = sc->arg[1];
            char hostname[128];
            uint32_t length;
            uint32_t resolved_address = 0;
            if (!user_writable(output_address, sizeof(resolved_address))) {
                sc->result = (uint32_t)-1;
                return;
            }
            for (length = 0; length < sizeof(hostname) - 1u; length++) {
                uint32_t current = hostname_address + length;
                if (current < hostname_address || !user_readable(current, 1)) {
                    sc->result = (uint32_t)-1;
                    return;
                }
                hostname[length] = *(const char*)current;
                if (!hostname[length]) break;
            }
            if (length == sizeof(hostname) - 1u || !hostname[0]) {
                sc->result = 0;
                return;
            }
            mutex_lock(&kernel_mutex);
            if (user_dns_active || !dns_resolve_async_start(hostname)) {
                mutex_unlock(&kernel_mutex);
                sc->result = 0;
                return;
            }
            user_dns_active = 1;
            user_dns_owner = arch_current_address_space();
            mutex_unlock(&kernel_mutex);
            int result;
            for (;;) {
                mutex_lock(&kernel_mutex);
                result = dns_resolve_async_poll(&resolved_address);
                if (result != 0) user_dns_active = 0;
                mutex_unlock(&kernel_mutex);
                if (result != 0) break;
                task_sleep_ms(1);
            }
            sc->result = result == 1;
            if (sc->result) *(uint32_t*)output_address = resolved_address;
            return;
        }
        case SYS_PING_START: {
            static const uint8_t payload[] = "operating-system";
            uint32_t owner_directory = arch_current_address_space();
            user_ping_request_t* request = 0;
            uint32_t handle;
            uint16_t identifier = 0;
            if (!sys_net_is_ready()) {
                sc->result = (uint32_t)-1;
                return;
            }
            for (uint32_t i = 0; i < USER_PING_REQUEST_MAX; i++) {
                if (!user_ping_requests[i].active) {
                    request = &user_ping_requests[i];
                    break;
                }
            }
            if (!request) {
                sc->result = (uint32_t)-1;
                return;
            }
            for (uint32_t attempt = 0; attempt < 0xFFFFu; attempt++) {
                uint16_t candidate = next_ping_identifier++;
                if (!next_ping_identifier) next_ping_identifier = 1;
                if (candidate && !ping_identifier_in_use(candidate)) {
                    identifier = candidate;
                    break;
                }
            }
            if (!identifier) {
                sc->result = (uint32_t)-1;
                return;
            }
            handle = next_ping_handle++;
            if (!handle || handle > 0x7FFFFFFFu) {
                next_ping_handle = 2;
                handle = 1;
            }
            request->owner_directory = owner_directory;
            request->handle = handle;
            request->destination = sc->arg[0];
            request->started_tick = frame_timer;
            request->reply_tick = 0;
            request->last_tick = frame_timer;
            request->stalled_polls = 0;
            request->identifier = identifier;
            request->sequence = (uint16_t)sc->arg[1];
            request->active = 1;
            request->completed = 0;
            request->timed_out = 0;
            icmp_set_echo_reply_handler(syscall_ping_reply);
            if (!icmp_request_echo(request->destination, request->identifier,
                                   request->sequence, payload,
                                   sizeof(payload) - 1u)) {
                request->active = 0;
                sc->result = (uint32_t)-1;
                return;
            }
            sc->result = handle;
            return;
        }
        case SYS_PING_POLL: {
            uint32_t handle = sc->arg[0];
            uint32_t output_address = sc->arg[1];
            if (!user_writable(output_address, sizeof(uint32_t))) {
                sc->result = (uint32_t)-1;
                return;
            }
            user_ping_request_t* request =
                ping_request_by_handle(handle, arch_current_address_space());
            if (!request) {
                sc->result = (uint32_t)-1;
                return;
            }
            if (!request->completed &&
                (uint32_t)(frame_timer - request->started_tick) >=
                    USER_PING_TIMEOUT_TICKS) {
                request->completed = 1;
                request->timed_out = 1;
            }
            if (frame_timer == request->last_tick) {
                if (++request->stalled_polls >= USER_PING_STALL_POLLS) {
                    request->completed = 1;
                    request->timed_out = 1;
                }
            } else {
                request->last_tick = frame_timer;
                request->stalled_polls = 0;
            }
            if (!request->completed) {
                sc->result = 0;
                return;
            }
            *(uint32_t*)output_address = request->timed_out ? 0
                : (request->reply_tick - request->started_tick) * 10u;
            sc->result = request->timed_out ? 2u : 1u;
            request->active = 0;
            return;
        }
        default:
            sc->result = (uint32_t)-1;
            return;
    }
}

/* I/O waiters hold only their device/filesystem lock, so they cannot prevent
 * the compositor or network worker from progressing while a device is busy. */
void syscall_dispatch(arch_syscall_t* sc) {
    if (!sc) return;
    /* Authorize before taking locks, touching user pointers or hardware.
     * Credentials belong to this task, inherited at task_create (including
     * sudo launches), and are never supplied by an executable or ABI argument. */
    if (!current_task || !sc->from_user) {
        sc->result = (uint32_t)-1;
        return;
    }
    if (!syscall_allowed(current_task->uid, sc->number, sc->arg[0])) {
        sc->result = (uint32_t)SYS_ERR_PERMISSION;
        return;
    }
    switch (sc->number) {
        case SYS_YIELD: case SYS_DNS_RESOLVE:
        case SYS_FS_CHMOD: case SYS_FS_DELETE: case SYS_FS_MKDIR: case SYS_FS_READ: case SYS_FS_READ_AT: case SYS_FS_LIST: case SYS_FS_LIST_DIR: case SYS_FS_WRITE:
        case SYS_FLOPPY_INIT: case SYS_FLOPPY_READ: case SYS_FLOPPY_TEST_WRITE:
            syscall_dispatch_impl(sc);
            return;
        case SYS_AUDIO_OPL:
        case SYS_AUDIO_CONFIGURE: case SYS_AUDIO_INIT: case SYS_AUDIO_WRITE: case SYS_AUDIO_STATUS: case SYS_AUDIO_SHUTDOWN: case SYS_AUDIO_DRAIN: {
            uint32_t call = sc->number;
            syscall_dispatch_impl(sc);
            if ((int32_t)sc->result < 0) {
                debug_audio_print("[AUDIO] syscall failed: number=");debug_audio_hex(call);
                debug_audio_print(" caller=");debug_audio_hex(arch_current_address_space());debug_audio_print("\n");
            }
            return;
        }
        default: {
            MUTEX_GUARD(kernel_mutex);
            syscall_dispatch_impl(sc);
            return;
        }
    }
}

void syscall_tcp_on_accept(tcp_socket_t* listener, tcp_socket_t* child) {
    for (uint32_t i = 0; i < USER_TCP_SOCKET_MAX; i++) {
        user_tcp_socket_t* slot = &user_tcp_sockets[i];
        if (slot->active && slot->is_listener && slot->socket == listener) {
            // Find a free user socket slot for the new connection
            user_tcp_socket_t* child_slot = 0;
            for (uint32_t j = 0; j < USER_TCP_SOCKET_MAX; j++) {
                if (!user_tcp_sockets[j].active) {
                    child_slot = &user_tcp_sockets[j];
                    break;
                }
            }
            if (!child_slot || slot->accept_count >= USER_TCP_BACKLOG) {
                tcp_release(child); 
                return;
            }

            uint32_t handle = next_tcp_handle++;
            if (!handle || handle > 0x7FFFFFFFu) {
                next_tcp_handle = 2;
                handle = 1;
            }

            child_slot->owner_directory = slot->owner_directory;
            child_slot->handle = handle;
            child_slot->socket = child;
            child_slot->read_head = 0;
            child_slot->read_count = 0;
            child_slot->active = 1;
            child_slot->is_listener = 0;
            tcp_bind(child, user_tcp_receive);

            uint32_t tail = (slot->accept_head + slot->accept_count) % USER_TCP_BACKLOG;
            slot->accepted_handles[tail] = handle;
            slot->accept_count++;
            return;
        }
    }
    tcp_release(child);
}
