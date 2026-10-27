#include "../system/services/audio/system_sound.h"
#include "hal/audio.h"
static unsigned int audio_owner;
#include "syscall.h"
#include "task.h"
#include "mutex.h"
#include "idt.h"
#include "paging.h"
#include "arch_i386.h"
#include "drivers/serial.h"
#include "drivers/power.h"
#include "hal/net_types.h"
#include "drivers/floppy.h"
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
#define USER_TCP_SOCKET_MAX 8u
#define USER_TCP_QUEUE_SIZE 131072u
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
    uint8_t data[USER_TCP_QUEUE_SIZE];
} user_tcp_socket_t;

static user_tcp_socket_t user_tcp_sockets[USER_TCP_SOCKET_MAX];
static uint32_t next_tcp_handle = 1;
static uint8_t user_dns_active;
static uint32_t user_dns_owner;
static uint32_t user_dns_handle;
static uint32_t next_dns_handle = 1;

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
    if (audio_owner == directory_address) { audio_shutdown(); audio_owner = 0; }
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
    if (!length || address < APP_VIRTUAL_BASE || address >= APP_VIRTUAL_LIMIT ||
        length > APP_VIRTUAL_LIMIT - address) return 0;

    uint32_t* directory = (uint32_t*)paging_current_directory();
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
    if (!length || address < APP_VIRTUAL_BASE || address >= APP_VIRTUAL_LIMIT ||
        length > APP_VIRTUAL_LIMIT - address) return 0;

    uint32_t* directory = (uint32_t*)paging_current_directory();
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

static void syscall_dispatch_impl(registers_t* regs) {
    if (!regs) return;
    if ((regs->cs & 3u) != 3u) {
        regs->eax = (uint32_t)-1;
        return;
    }
    switch (regs->eax) {
        case SYS_WRITE: {
            uint32_t address = regs->ebx;
            uint32_t length = regs->ecx;
            if (length > SYS_WRITE_MAX || !user_readable(address, length)) {
                regs->eax = (uint32_t)-1;
                return;
            }
            const char* text = (const char*)address;
            if (!current_task->detached) kernel_user_write(text, length);
            regs->eax = length;
            return;
        }
        case SYS_GET_TICKS:
            regs->eax = frame_timer;
            return;
        case SYS_MEMORY_INFO: {
            if (!user_writable(regs->ebx, sizeof(user_memory_info_t))) {
                regs->eax = (uint32_t)-1;
                return;
            }
            user_memory_info_t snapshot;
            paging_memory_info(&snapshot);
            *(user_memory_info_t*)regs->ebx = snapshot;
            regs->eax = 0;
            return;
        }
        case SYS_DNS_START: {
            char hostname[254];
            uint32_t address = regs->ebx;
            uint32_t length = 0;
            if (user_dns_active) {
                regs->eax = (uint32_t)-1;
                return;
            }
            while (length < sizeof(hostname) - 1u) {
                uint32_t current = address + length;
                if (current < address || !user_readable(current, 1)) {
                    regs->eax = (uint32_t)-1;
                    return;
                }
                hostname[length] = *(const char*)current;
                if (!hostname[length]) break;
                length++;
            }
            if (!length || length == sizeof(hostname) - 1u ||
                !dns_resolve_async_start(hostname)) {
                regs->eax = (uint32_t)-1;
                return;
            }
            uint32_t handle = next_dns_handle++;
            if (!handle || handle > 0x7FFFFFFFu) {
                next_dns_handle = 2;
                handle = 1;
            }
            user_dns_active = 1;
            user_dns_owner = paging_current_directory();
            user_dns_handle = handle;
            regs->eax = handle;
            return;
        }
        case SYS_DNS_POLL: {
            uint32_t output_address = regs->ecx;
            uint32_t resolved = 0;
            if (!user_dns_active || user_dns_handle != regs->ebx ||
                user_dns_owner != paging_current_directory() ||
                !user_writable(output_address, sizeof(resolved))) {
                regs->eax = (uint32_t)-1;
                return;
            }
            int result = dns_resolve_async_poll(&resolved);
            if (result == 1) *(uint32_t*)output_address = resolved;
            if (result != 0) user_dns_active = 0;
            regs->eax = result == 1 ? 1u : result == 0 ? 0u : 2u;
            return;
        }
        case SYS_SERIAL_LOG_READ: {
            if (!regs->ecx || regs->ecx > 256 || !user_writable(regs->ebx, regs->ecx) ||
                !user_writable(regs->edx, sizeof(user_serial_cursor_t))) {
                regs->eax = (uint32_t)-1;
                return;
            }
            user_serial_cursor_t cursor = *(user_serial_cursor_t*)regs->edx;
            regs->eax = serial_log_read((char*)regs->ebx, regs->ecx, &cursor.next, &cursor.lost);
            *(user_serial_cursor_t*)regs->edx = cursor;
            return;
        }
        case SYS_AUDIO_DEVICE_NAME: {
            uint32_t capacity = regs->ecx;
            if (!capacity || capacity > 64 || !user_writable(regs->ebx, capacity)) {
                regs->eax = (uint32_t)-1;
                return;
            }
            const char* name = audio_device_name(audio_device_selected());
            char* output = (char*)regs->ebx;
            uint32_t n = 0;
            while (n + 1 < capacity && name[n]) { output[n] = name[n]; ++n; }
            output[n] = 0;
            regs->eax = n;
            return;
        }
        case SYS_AUDIO_OPL:
            if (regs->ebx == 256) {
                regs->eax = (uint32_t)(audio_owner ? -1 : audio_opl_init());
                if ((int32_t)regs->eax == 0) audio_owner = paging_current_directory();
            } else {
                regs->eax = (uint32_t)(audio_owner == paging_current_directory()
                    ? audio_opl_write(regs->ebx, regs->ecx) : -1);
            }
            return;
        case SYS_AUDIO_INIT:
            regs->eax = (uint32_t)(audio_owner ? -1 : audio_init(regs->ebx));
            if ((int32_t)regs->eax == 0) audio_owner = paging_current_directory();
            return;
        case SYS_AUDIO_WRITE:
            regs->eax = (uint32_t)((audio_owner != paging_current_directory() ||
                regs->ecx > AUDIO_BUFFER_CHUNK_SIZE || !user_readable(regs->ebx, regs->ecx))
                ? -1 : audio_write((const void*)regs->ebx, regs->ecx));
            return;
        case SYS_AUDIO_DRAIN:
            regs->eax = (uint32_t)(audio_owner == paging_current_directory() ? audio_drain() : -1);
            return;
        case SYS_AUDIO_STATUS:
            regs->eax = (uint32_t)(audio_owner == paging_current_directory() ? audio_status() : -1);
            return;
        case SYS_AUDIO_SHUTDOWN:
            regs->eax = (uint32_t)-1;
            if (audio_owner == paging_current_directory()) {
                audio_shutdown(); audio_owner = 0; regs->eax = 0;
            }
            return;
        case SYS_FS_READ_AT:
        case SYS_FS_READ: {
            char filename[FS_MAX_PATH];
            uint32_t filename_address = regs->ebx;
            uint32_t output_address = regs->ecx;
            uint32_t capacity = regs->edx;
            uint32_t length = 0;
            if (capacity > SYS_FS_READ_MAX ||
                (capacity && !user_writable(output_address, capacity)) ||
                (!capacity && output_address)) {
                regs->eax = (uint32_t)-1;
                return;
            }
            while (length < sizeof(filename) - 1u) {
                uint32_t current = filename_address + length;
                if (current < filename_address || !user_readable(current, 1)) {
                    regs->eax = (uint32_t)-1;
                    return;
                }
                filename[length] = *(const char*)current;
                if (!filename[length]) break;
                length++;
            }
            if (!length || length == sizeof(filename) - 1u) {
                regs->eax = (uint32_t)-1;
                return;
            }
            regs->eax = (uint32_t)fs_read_file_at(filename,
                capacity ? (uint8_t*)output_address : 0, capacity,
                regs->eax == SYS_FS_READ_AT ? regs->esi : 0);
            return;
        }
        case SYS_FS_LIST_DIR: {
            char path[FS_MAX_PATH];
            if (!copy_user_text(regs->ebx, path, sizeof(path)) || !path[0] ||
                !user_writable(regs->ecx, FS_MAX_FILES*FS_MAX_FILENAME) ||
                !user_writable(regs->edx, FS_MAX_FILES) ||
                !fs_access(path, FS_ACCESS_READ | FS_ACCESS_EXEC) ||
                (!(path[0] == '/' && !path[1]) && !fs_is_directory(path))) {
                regs->eax = (uint32_t)-1; return;
            }
            regs->eax = fs_list_dir_types(path, (char (*)[FS_MAX_FILENAME])regs->ecx, (uint8_t*)regs->edx);
            return;
        }
        case SYS_FILE_PICK_RESULT: {
            char path[FS_MAX_PATH];
            if (!copy_user_text(regs->ecx, path, sizeof(path))) {
                regs->eax = (uint32_t)-1; return;
            }
            regs->eax = desktop_picker_result(regs->ebx, current_task->id, path);
            return;
        }
        case SYS_APP_LAUNCH: {
            char path[FS_MAX_PATH], arguments[256];
            if (!copy_user_text(regs->ebx, path, sizeof(path)) || !path[0] ||
                !copy_user_text(regs->ecx, arguments, sizeof(arguments))) {
                regs->eax = (uint32_t)-1; return;
            }
            regs->eax = app_loader_launch_detached(kernel_get_api(), path, arguments);
            return;
        }
        case SYS_FS_LIST: {
            uint32_t output_address = regs->ebx;
            uint32_t output_size = FS_MAX_FILES * FS_MAX_FILENAME;
            if (!user_writable(output_address, output_size)) {
                regs->eax = (uint32_t)-1;
                return;
            }
            regs->eax = (uint32_t)fs_list(
                (char (*)[FS_MAX_FILENAME])output_address);
            return;
        }
        case SYS_FS_WRITE: {
            char filename[FS_MAX_PATH];
            uint32_t filename_address = regs->ebx;
            uint32_t source_address = regs->ecx;
            uint32_t length = regs->edx;
            uint32_t name_length = 0;
            if (!length || length > SYS_FS_WRITE_MAX ||
                !user_readable(source_address, length)) {
                regs->eax = (uint32_t)-1;
                return;
            }
            while (name_length < sizeof(filename) - 1u) {
                uint32_t current = filename_address + name_length;
                if (current < filename_address || !user_readable(current, 1)) {
                    regs->eax = (uint32_t)-1;
                    return;
                }
                filename[name_length] = *(const char*)current;
                if (!filename[name_length]) break;
                name_length++;
            }
            if (!name_length || name_length == sizeof(filename) - 1u) {
                regs->eax = (uint32_t)-1;
                return;
            }
            regs->eax = (uint32_t)fs_write_file(filename,
                (const uint8_t*)source_address, length);
            return;
        }
        case SYS_TCP_CONNECT: {
            uint32_t owner_directory = paging_current_directory();
            user_tcp_socket_t* slot = 0;
            for (uint32_t i = 0; i < USER_TCP_SOCKET_MAX; i++) {
                if (!user_tcp_sockets[i].active) {
                    slot = &user_tcp_sockets[i];
                    break;
                }
            }
            if (!slot || !regs->ecx || regs->ecx > 65535u) {
                regs->eax = (uint32_t)-1;
                return;
            }
            tcp_socket_t* socket = tcp_connect(regs->ebx, (uint16_t)regs->ecx);
            if (!socket) {
                regs->eax = (uint32_t)-1;
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
            regs->eax = handle;
            return;
        }
        case SYS_TCP_STATE: {
            user_tcp_socket_t* slot = user_tcp_by_handle(
                regs->ebx, paging_current_directory());
            regs->eax = slot ? (uint32_t)tcp_get_state(slot->socket)
                             : (uint32_t)-1;
            return;
        }
        case SYS_TCP_SEND: {
            user_tcp_socket_t* slot = user_tcp_by_handle(
                regs->ebx, paging_current_directory());
            uint32_t length = regs->edx;
            if (!slot || length > SYS_TCP_SEND_MAX ||
                (length && !user_readable(regs->ecx, length))) {
                regs->eax = 0;
                return;
            }
            regs->eax = (uint32_t)tcp_send(slot->socket,
                length ? (const uint8_t*)regs->ecx : 0, (uint16_t)length);
            return;
        }
        case SYS_TCP_READ: {
            user_tcp_socket_t* slot = user_tcp_by_handle(
                regs->ebx, paging_current_directory());
            uint32_t capacity = regs->edx;
            if (!slot || !capacity || capacity > SYS_TCP_READ_MAX ||
                !user_writable(regs->ecx, capacity)) {
                regs->eax = (uint32_t)-1;
                return;
            }
            uint32_t count = slot->read_count < capacity
                           ? slot->read_count : capacity;
            uint8_t* output = (uint8_t*)regs->ecx;
            for (uint32_t i = 0; i < count; i++) {
                output[i] = slot->data[slot->read_head];
                slot->read_head = (slot->read_head + 1u) % USER_TCP_QUEUE_SIZE;
            }
            slot->read_count -= count;
            regs->eax = count;
            return;
        }
        case SYS_TCP_CLOSE: {
            user_tcp_socket_t* slot = user_tcp_by_handle(
                regs->ebx, paging_current_directory());
            if (!slot) {
                regs->eax = (uint32_t)-1;
                return;
            }
            tcp_release(slot->socket);
            slot->active = 0;
            regs->eax = 0;
            return;
        }
        case SYS_FLOPPY_INIT:
            regs->eax = (uint32_t)floppy_init();
            return;
        case SYS_FLOPPY_READ: {
            uint32_t lba = regs->ebx;
            uint32_t count = regs->ecx;
            uint32_t buffer = regs->edx;
            if (!count || count > 18u || lba >= FLOPPY_SECTOR_COUNT ||
                count > FLOPPY_SECTOR_COUNT - lba ||
                !user_writable(buffer, count * FLOPPY_SECTOR_SIZE)) {
                regs->eax = 0;
                return;
            }
            regs->eax = (uint32_t)floppy_read_sectors(lba, (uint8_t)count,
                                                     (uint8_t*)buffer);
            return;
        }
        case SYS_FLOPPY_TEST_WRITE: {
            uint8_t pattern[FLOPPY_SECTOR_SIZE];
            for (uint32_t i = 0; i < FLOPPY_SECTOR_SIZE; i++)
                pattern[i] = (uint8_t)(i ^ 0xA5u);
            regs->eax = (uint32_t)floppy_write_sectors(2879u, 1, pattern);
            return;
        }
        case SYS_YIELD:
            /* The desktop task owns service pumping. Park this waiter rather
             * than recursively entering WM callbacks on the app's stack. */
            task_sleep_ms(1);
            regs->eax = 0;
            return;
        case SYS_WINDOW_CREATE: {
            user_window_desc_t description;
            uint32_t address = regs->ebx;
            if (!user_readable(address, sizeof(description))) {
                regs->eax = (uint32_t)-1;
                return;
            }
            description = *(const user_window_desc_t*)address;
            uint32_t title_length = 0;
            while (title_length < USER_WINDOW_TITLE_MAX && description.title[title_length])
                title_length++;
            if (title_length == USER_WINDOW_TITLE_MAX) {
                regs->eax = (uint32_t)-1;
                return;
            }
            regs->eax = (uint32_t)wm_user_window_create(
                &description, paging_current_directory());
            return;
        }
        case SYS_WINDOW_DRAW: {
            user_window_draw_t drawing;
            char text[64];
            const char* text_pointer = 0;
            const uint8_t* image_pointer = 0;
            uint32_t address = regs->ecx;
            if (!user_readable(address, sizeof(drawing))) {
                regs->eax = (uint32_t)-1;
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
                        regs->eax = (uint32_t)-1;
                        return;
                    }
                    text[length] = drawing.text[length];
                    if (!text[length]) break;
                    length++;
                }
                if (length == sizeof(text)) {
                    regs->eax = (uint32_t)-1;
                    return;
                }
                text_pointer = text;
            } else if (drawing.operation == USER_WINDOW_DRAW_IMAGE || drawing.operation == USER_WINDOW_DRAW_IMAGE_CLIPPED) {
                uint32_t image_address = (uint32_t)drawing.data;
                if (!drawing.data_length || drawing.data_length > SYS_WINDOW_IMAGE_MAX ||
                    !user_readable(image_address, drawing.data_length)) {
                    regs->eax = (uint32_t)-1;
                    return;
                }
                image_pointer = (const uint8_t*)image_address;
            }
            regs->eax = (uint32_t)wm_user_window_draw(
                regs->ebx, paging_current_directory(), &drawing, text_pointer,
                image_pointer);
            return;
        }
        case SYS_WINDOW_POLL: {
            uint32_t output_address = regs->ecx;
            if (!user_writable(output_address, sizeof(user_window_event_t))) {
                regs->eax = (uint32_t)-1;
                return;
            }
            regs->eax = (uint32_t)wm_user_window_poll(
                regs->ebx, paging_current_directory(),
                (user_window_event_t*)output_address);
            return;
        }
        case SYS_WINDOW_PRESENT:
            regs->eax = (uint32_t)wm_user_window_present(
                regs->ebx, paging_current_directory());
            return;
        case SYS_WINDOW_CLOSE:
            regs->eax = (uint32_t)wm_user_window_close(
                regs->ebx, paging_current_directory());
            return;
        case SYS_DETACH:
            regs->eax = task_detach();
            return;
        case SYS_GETPID:
            regs->eax = current_task->id;
            return;
        case SYS_GETPPID:
            regs->eax = task_parent_id();
            return;
        case SYS_EXIT:
            arch_user_exit_requested = 1;
            return;
        case SYS_SHUTDOWN:
            power_shutdown();
            regs->eax = (uint32_t)-1;
            return;
        case SYS_REBOOT:
            power_reboot();
            regs->eax = (uint32_t)-1;
            return;
        case SYS_NET_DIAGNOSTICS: {
            uint32_t address = regs->ebx;
            if (!user_writable(address, sizeof(net_diagnostics_t))) {
                regs->eax = (uint32_t)-1;
                return;
            }
            sys_net_poll();
            sys_net_get_diagnostics((net_diagnostics_t*)address);
            regs->eax = sys_net_is_ready() ? 1u : 0u;
            return;
        }
        case SYS_DNS_RESOLVE: {
            uint32_t hostname_address = regs->ebx;
            uint32_t output_address = regs->ecx;
            char hostname[128];
            uint32_t length;
            uint32_t resolved_address = 0;
            if (!user_writable(output_address, sizeof(resolved_address))) {
                regs->eax = (uint32_t)-1;
                return;
            }
            for (length = 0; length < sizeof(hostname) - 1u; length++) {
                uint32_t current = hostname_address + length;
                if (current < hostname_address || !user_readable(current, 1)) {
                    regs->eax = (uint32_t)-1;
                    return;
                }
                hostname[length] = *(const char*)current;
                if (!hostname[length]) break;
            }
            if (length == sizeof(hostname) - 1u || !hostname[0]) {
                regs->eax = 0;
                return;
            }
            mutex_lock(&kernel_mutex);
            if (user_dns_active || !dns_resolve_async_start(hostname)) {
                mutex_unlock(&kernel_mutex);
                regs->eax = 0;
                return;
            }
            user_dns_active = 1;
            user_dns_owner = paging_current_directory();
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
            regs->eax = result == 1;
            if (regs->eax) *(uint32_t*)output_address = resolved_address;
            return;
        }
        case SYS_PING_START: {
            static const uint8_t payload[] = "operating-system";
            uint32_t owner_directory = paging_current_directory();
            user_ping_request_t* request = 0;
            uint32_t handle;
            uint16_t identifier = 0;
            if (!sys_net_is_ready()) {
                regs->eax = (uint32_t)-1;
                return;
            }
            for (uint32_t i = 0; i < USER_PING_REQUEST_MAX; i++) {
                if (!user_ping_requests[i].active) {
                    request = &user_ping_requests[i];
                    break;
                }
            }
            if (!request) {
                regs->eax = (uint32_t)-1;
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
                regs->eax = (uint32_t)-1;
                return;
            }
            handle = next_ping_handle++;
            if (!handle || handle > 0x7FFFFFFFu) {
                next_ping_handle = 2;
                handle = 1;
            }
            request->owner_directory = owner_directory;
            request->handle = handle;
            request->destination = regs->ebx;
            request->started_tick = frame_timer;
            request->reply_tick = 0;
            request->last_tick = frame_timer;
            request->stalled_polls = 0;
            request->identifier = identifier;
            request->sequence = (uint16_t)regs->ecx;
            request->active = 1;
            request->completed = 0;
            request->timed_out = 0;
            icmp_set_echo_reply_handler(syscall_ping_reply);
            if (!icmp_request_echo(request->destination, request->identifier,
                                   request->sequence, payload,
                                   sizeof(payload) - 1u)) {
                request->active = 0;
                regs->eax = (uint32_t)-1;
                return;
            }
            regs->eax = handle;
            return;
        }
        case SYS_PING_POLL: {
            uint32_t handle = regs->ebx;
            uint32_t output_address = regs->ecx;
            if (!user_writable(output_address, sizeof(uint32_t))) {
                regs->eax = (uint32_t)-1;
                return;
            }
            user_ping_request_t* request =
                ping_request_by_handle(handle, paging_current_directory());
            if (!request) {
                regs->eax = (uint32_t)-1;
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
                regs->eax = 0;
                return;
            }
            *(uint32_t*)output_address = request->timed_out ? 0
                : (request->reply_tick - request->started_tick) * 10u;
            regs->eax = request->timed_out ? 2u : 1u;
            request->active = 0;
            return;
        }
        default:
            regs->eax = (uint32_t)-1;
            return;
    }
}

/* I/O waiters hold only their device/filesystem lock, so they cannot prevent
 * the compositor or network worker from progressing while a device is busy. */
void syscall_dispatch(registers_t* regs) {
    if (!regs) return;
    switch (regs->eax) {
        case SYS_YIELD: case SYS_DNS_RESOLVE:
        case SYS_FS_READ: case SYS_FS_READ_AT: case SYS_FS_LIST: case SYS_FS_LIST_DIR: case SYS_FS_WRITE:
        case SYS_FLOPPY_INIT: case SYS_FLOPPY_READ: case SYS_FLOPPY_TEST_WRITE:
            syscall_dispatch_impl(regs);
            return;
        case SYS_AUDIO_OPL:
        case SYS_AUDIO_INIT: case SYS_AUDIO_WRITE: case SYS_AUDIO_STATUS: case SYS_AUDIO_SHUTDOWN: case SYS_AUDIO_DRAIN: {
            uint32_t call = regs->eax;
            syscall_dispatch_impl(regs);
            if ((int32_t)regs->eax < 0) {
                debug_audio_print("[AUDIO] syscall failed: number=");debug_audio_hex(call);
                debug_audio_print(" owner=");debug_audio_hex(audio_owner);
                debug_audio_print(" caller=");debug_audio_hex(paging_current_directory());debug_audio_print("\n");
            }
            return;
        }
        default: {
            MUTEX_GUARD(kernel_mutex);
            syscall_dispatch_impl(regs);
            return;
        }
    }
}
