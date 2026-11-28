#include "types.h"
#include <stddef.h>
#include "arch/arch.h"
#include "task.h"
#include "os_api.h"
#include "drivers/acpi.h"
#include "drivers/nvme.h"
#include "drivers/ahci.h"
#include "drivers/floppy.h"
#include "drivers/power.h"
#include "drivers/rtc.h"
#include "drivers/delay.h"
#include "drivers/usb/ehci.h"
#include "drivers/usb/uhci.h"
#include "hal/net.h"
#include "hal/audio.h"
#include "hal/wifi.h"
#include "net/ipv4.h"
#include "net/icmp.h"
#include "net/dns.h"
#include "net/ntp.h"
#include "net/tcp.h"
#include "drivers/pci.h"
#include "../../../system/shell/commands.h"
#include "../../../system/services/audio/mixer.h"
#include "../../../system/services/audio/system_sound.h"
#include "../../../system/graphics/dialog/crash_dialog.h"

user_crash_info_t last_user_crash;

int acpi_init(void) { return 0; }
uint8_t acpi_rtc_century_register(void) { return 0; }
int nvme_init(void) { return 0; }
void ahci_init(void) { }

int floppy_init(void) { return 0; }
int floppy_read_sectors(uint32_t lba, uint8_t count, uint8_t* buffer) {
    (void)lba; (void)count; (void)buffer;
    return 0;
}
int floppy_write_sectors(uint32_t lba, uint8_t count, const uint8_t* buffer) {
    (void)lba; (void)count; (void)buffer;
    return 0;
}

void power_shutdown(void) { arch_halt_forever(); }
void power_reboot(void) { arch_halt_forever(); }

void usb_hal_init(void) { }
void usb_hal_register(const usb_hci_ops_t* driver) { (void)driver; }
void usb_poll(void) { }
const usb_hci_ops_t* ehci_get_ops(void) { return 0; }
const usb_hci_ops_t* uhci_get_ops(void) { return 0; }

void b44_set_init_progress(void (*progress)(const char* status)) { (void)progress; }
void r8169_set_init_progress(void (*progress)(const char* status)) { (void)progress; }

void driver_delay_us(uint32_t microseconds) {
    volatile uint32_t count = microseconds * 200u;
    while (count--) arch_cpu_relax();
}

uint32_t net_driver_type = ~0u;
const pci_device_t* sys_net_pci_device(void) { return 0; }
void sys_net_get_device_info(uint16_t* vendor_out, uint16_t* device_out, const char** name_out) {
    if (vendor_out) *vendor_out = 0;
    if (device_out) *device_out = 0;
    if (name_out) *name_out = "none";
}
uint8_t sys_net_init(uint16_t io) { (void)io; return 0; }
uint8_t sys_net_is_ready(void) { return 0; }
void sys_net_shutdown(void) { }
void sys_net_poll(void) { }
void sys_net_wait(void) { task_sleep_ms(50); }
uint8_t sys_net_send(const uint8_t* data, uint32_t length) { (void)data; (void)length; return 0; }
void sys_net_set_receive_handler(net_receive_handler_t handler) { (void)handler; }
uint64_t sys_net_get_mac_address(void) { return 0; }
void sys_net_set_ip_address(uint32_t address) { (void)address; }
uint32_t sys_net_get_ip_address(void) { return 0; }
void sys_net_get_diagnostics(net_diagnostics_t* diagnostics) {
    if (!diagnostics) return;
    for (uint32_t i = 0; i < sizeof(*diagnostics); i++) ((uint8_t*)diagnostics)[i] = 0;
}
uint8_t sys_net_link_is_ready(sys_net_link_t link) { (void)link; return 0; }
uint8_t sys_net_link_send(sys_net_link_t link, const uint8_t* data, uint32_t length) {
    (void)link; (void)data; (void)length;
    return 0;
}
void sys_net_link_set_receive_handler(sys_net_link_t link, net_receive_handler_t handler) {
    (void)link; (void)handler;
}
uint64_t sys_net_link_mac(sys_net_link_t link) { (void)link; return 0; }
uint32_t sys_net_link_ip(sys_net_link_t link) { (void)link; return 0; }
uint8_t sys_net_select_wifi(void) { return 0; }
void sys_net_wifi_down(void) { }
void sys_net_initialize_stack(void) { }
void sys_net_configure_address(void) { }
uint8_t sys_net_set_manual_configuration(uint32_t address, uint32_t mask, uint32_t gateway) {
    (void)address; (void)mask; (void)gateway;
    return 0;
}
void sys_net_get_configuration(uint32_t* subnet_mask, uint32_t* gateway, uint8_t* dhcp_leased) {
    if (subnet_mask) *subnet_mask = 0;
    if (gateway) *gateway = 0;
    if (dhcp_leased) *dhcp_leased = 0;
}

void net_rx_signal(void) { }
void net_rx_backlog(void) { }

uint8_t ipv4_send(uint32_t destination_ip, uint8_t protocol,
                  const uint8_t* payload, uint32_t length) {
    (void)destination_ip; (void)protocol; (void)payload; (void)length;
    return 0;
}
uint32_t ipv4_get_local_ip(void) { return 0; }
void ipv4_set_protocol_handler(uint8_t protocol, ipv4_protocol_handler_t handler) {
    (void)protocol; (void)handler;
}
void icmp_set_echo_reply_handler(icmp_echo_reply_handler_t handler) { (void)handler; }
uint8_t icmp_request_echo(uint32_t destination_ip, uint16_t identifier,
                          uint16_t sequence, const uint8_t* payload, uint16_t length) {
    (void)destination_ip; (void)identifier; (void)sequence; (void)payload; (void)length;
    return 0;
}
uint8_t dns_resolve(const char* hostname, uint32_t* address) {
    (void)hostname;
    if (address) *address = 0;
    return 0;
}
uint8_t dns_resolve_async_start(const char* hostname) { (void)hostname; return 0; }
int dns_resolve_async_poll(uint32_t* address) {
    if (address) *address = 0;
    return 0;
}
void dns_resolve_async_cancel(void) { }
void ipv4_clear_handlers_for_directory(uint32_t directory_address) { (void)directory_address; }

tcp_socket_t* tcp_connect(uint32_t remote_ip, uint16_t remote_port) {
    (void)remote_ip; (void)remote_port;
    return 0;
}
tcp_socket_t* tcp_listen(uint16_t local_port) { (void)local_port; return 0; }
void tcp_bind(tcp_socket_t* socket, tcp_data_handler_t handler) { (void)socket; (void)handler; }
void tcp_resume(tcp_socket_t* socket) { (void)socket; }
uint8_t tcp_send(tcp_socket_t* socket, const uint8_t* data, uint16_t length) {
    (void)socket; (void)data; (void)length;
    return 0;
}
void tcp_disconnect(tcp_socket_t* socket) { (void)socket; }
void tcp_release(tcp_socket_t* socket) { (void)socket; }
tcp_socket_state_t tcp_get_state(const tcp_socket_t* socket) {
    (void)socket;
    return TCP_CLOSED;
}
uint32_t tcp_get_remote_ip(const tcp_socket_t* socket) { (void)socket; return 0; }

uint32_t pci_device_count(void) { return 0; }
const pci_device_t* pci_device_at(uint32_t index) { (void)index; return 0; }

void time_init(void) { }
void ntp_poll(void) { }
int time_configure(int enabled, const char* server, int offset_minutes) {
    (void)enabled; (void)server; (void)offset_minutes;
    return 0;
}
int time_sync_enabled(void) { return 0; }
const char* time_server(void) { return ""; }
int time_zone_offset(void) { return 0; }
const char* time_sync_status(void) { return "disabled"; }
void time_sync_now(void) { }
int time_set_local(const rtc_time_t* time) { (void)time; return 0; }
uint64_t time_to_epoch(const rtc_time_t* time) { (void)time; return 0; }
void time_from_epoch(uint64_t seconds, rtc_time_t* time) {
    (void)seconds;
    if (time) for (uint32_t i = 0; i < sizeof(*time); i++) ((uint8_t*)time)[i] = 0;
}
void rtc_set_epoch(uint64_t seconds) { (void)seconds; }
uint64_t rtc_get_epoch(void) { return 0; }
void rtc_get_time(rtc_time_t* out) {
    if (out) for (uint32_t i = 0; i < sizeof(*out); i++) ((uint8_t*)out)[i] = 0;
}
void rtc_format_time_hm(char out[6]) {
    if (!out) return;
    out[0] = '0'; out[1] = '0'; out[2] = ':'; out[3] = '0'; out[4] = '0'; out[5] = 0;
}

void audio_hal_init(void) { }
void audio_hal_register(const audio_driver_ops_t* driver) { (void)driver; }
uint32_t audio_device_count(void) { return 0; }
const char* audio_device_name(uint32_t index) { (void)index; return "none"; }
uint32_t audio_device_selected(void) { return 0; }
int32_t audio_device_select(uint32_t index) { (void)index; return -1; }
uint32_t audio_get_volume(void) { return 0; }
void audio_set_volume(uint32_t volume) { (void)volume; }
int32_t audio_init(uint32_t sample_rate) { (void)sample_rate; return -1; }
int32_t audio_write(const void* data, uint32_t length) { (void)data; (void)length; return -1; }
int32_t audio_status(void) { return 0; }
int32_t audio_pending_frames(void) { return 0; }
int32_t audio_drain(void) { return 0; }
void audio_service(void) { }
int32_t audio_opl_init(void) { return -1; }
int32_t audio_opl_write(uint32_t reg, uint32_t value) { (void)reg; (void)value; return -1; }
void audio_opl_shutdown(void) { }
void audio_shutdown(void) { }
void audio_interrupt(uint8_t int_no) { (void)int_no; }

int32_t audio_mixer_opl(uint32_t owner, uint32_t reg, uint32_t value) {
    (void)owner; (void)reg; (void)value;
    return -1;
}
int32_t audio_mixer_open(uint32_t owner, const user_audio_format_t* format) {
    (void)owner; (void)format;
    return -1;
}
int32_t audio_mixer_write(uint32_t owner, const void* data, uint32_t length) {
    (void)owner; (void)data; (void)length;
    return -1;
}
int32_t audio_mixer_status(uint32_t owner) { (void)owner; return 0; }
int32_t audio_mixer_drain(uint32_t owner) { (void)owner; return 0; }
void audio_mixer_close(uint32_t owner) { (void)owner; }
void audio_mixer_service(void) { }
int32_t audio_mixer_device_select(uint32_t index) { (void)index; return -1; }

void system_sound_start(void) { }
void system_sound_error(void) { }
void system_sound_poll(void) { }

void sys_wifi_probe(void) { }
void sys_wifi_get_status(wifi_status_t* out) {
    if (out) for (uint32_t i = 0; i < sizeof(*out); i++) ((uint8_t*)out)[i] = 0;
}
const char* sys_wifi_status_text(void) { return "No Wi-Fi adapter\n"; }
unsigned sys_wifi_network_count(void) { return 0; }
int sys_wifi_get_network(unsigned index, il_network_t* out) {
    (void)index; (void)out;
    return 0;
}
const char* sys_wifi_scan(void) { return "No Wi-Fi adapter\n"; }
const char* sys_wifi_connect(unsigned network, const uint8_t* password, unsigned length) {
    (void)network; (void)password; (void)length;
    return "No Wi-Fi adapter\n";
}
void sys_wifi_disconnect(void) { }
int sys_wifi_is_connected(void) { return 0; }
void sys_wifi_poll(void) { }

void shell_execute_command(const char* command, shell_output_fn output,
                           shell_clear_fn clear, int (*read_reply)(char*, uint32_t)) {
    (void)command; (void)clear; (void)read_reply;
    if (output) output("shell is not available on this target\n");
}
const char* shell_command_args(void) { return ""; }
void shell_format_prompt(terminal_t* terminal, char* out, unsigned capacity) {
    (void)terminal;
    if (!out || !capacity) return;
    const char* prompt = "$ ";
    unsigned i = 0;
    while (prompt[i] && i + 1 < capacity) {
        out[i] = prompt[i];
        i++;
    }
    out[i] = 0;
}
void shell_initialize(os_api_t* api) { (void)api; }
int shell_submit_command(terminal_t* terminal, const char* command,
                         shell_output_fn output, shell_clear_fn clear) {
    (void)terminal; (void)command; (void)output; (void)clear;
    return 0;
}
int shell_open_terminal(terminal_t* terminal) { (void)terminal; return 0; }
void shell_close_terminal(terminal_t* terminal) { (void)terminal; }
int shell_interrupt(terminal_t* terminal) { (void)terminal; return 0; }
void shell_service(void* argument) { (void)argument; }

void* wifi_crypto_memcpy(void* out, const void* in, size_t length) {
    uint8_t* d = out;
    const uint8_t* s = in;
    while (length--) *d++ = *s++;
    return out;
}

void* wifi_crypto_memmove(void* out, const void* in, size_t length) {
    uint8_t* d = out;
    const uint8_t* s = in;
    if ((unsigned long)d < (unsigned long)s) return wifi_crypto_memcpy(out, in, length);
    while (length) {
        length--;
        d[length] = s[length];
    }
    return out;
}

void* wifi_crypto_memset(void* out, int value, size_t length) {
    uint8_t* d = out;
    while (length--) *d++ = (uint8_t)value;
    return out;
}

int wifi_crypto_memcmp(const void* a, const void* b, size_t length) {
    const uint8_t* x = a;
    const uint8_t* y = b;
    while (length--) {
        if (*x != *y) return *x - *y;
        x++;
        y++;
    }
    return 0;
}

size_t wifi_crypto_strlen(const char* text) {
    size_t n = 0;
    while (text[n]) n++;
    return n;
}

void wifi_crypto_zero(void* memory, size_t length) {
    volatile uint8_t* p = memory;
    while (length--) *p++ = 0;
}
