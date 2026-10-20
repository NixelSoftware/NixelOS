#include "syscall.h"
#include "idt.h"
#include "paging.h"
#include "arch_i386.h"
#include "drivers/serial.h"
#include "drivers/power.h"
#include "drivers/net.h"
#include "kernel_net.h"
#include "net/dns.h"
#include "net/icmp.h"

#define SYS_WRITE_MAX 256u
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
    for (uint32_t i = 0; i < USER_PING_REQUEST_MAX; i++) {
        if (user_ping_requests[i].owner_directory == directory_address)
            user_ping_requests[i].active = 0;
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

void syscall_dispatch(registers_t* regs) {
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
            kernel_user_write(text, length);
            for (uint32_t i = 0; i < length; i++) debug_putc(text[i]);
            regs->eax = length;
            return;
        }
        case SYS_GET_TICKS:
            regs->eax = frame_timer;
            return;
        case SYS_EXIT:
            arch_user_exit_requested = 1;
            return;
        case SYS_SHUTDOWN:
            power_shutdown();
            regs->eax = (uint32_t)-1;
            return;
        case SYS_NET_DIAGNOSTICS: {
            uint32_t address = regs->ebx;
            if (!user_writable(address, sizeof(net_diagnostics_t))) {
                regs->eax = (uint32_t)-1;
                return;
            }
            net_poll();
            net_get_diagnostics((net_diagnostics_t*)address);
            regs->eax = net_is_ready() ? 1u : 0u;
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
            regs->eax = dns_resolve(hostname, &resolved_address);
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
            sys_net_poll();
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
