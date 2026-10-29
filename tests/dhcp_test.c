/* Host regression test: cc -I. tests/dhcp_test.c -o /tmp/dhcp-test && /tmp/dhcp-test */
#include "src/kernel/net/dhcp.c"

mutex_t kernel_mutex;
static int lock_depth, failures, sleeps, scheduled = 1, drop, connected;
static uint64_t test_clock;
static uint32_t test_ip, test_mask, test_gateway;
static uint8_t outgoing_type;
static udp_data_handler_t receiver;
struct udp_socket { int unused; } test_socket;
#define CHECK(condition) do { if (!(condition)) failures++; } while (0)
void mutex_lock(mutex_t* lock) { (void)lock; lock_depth++; }
void mutex_unlock(mutex_t* lock) { (void)lock; lock_depth--; }
int scheduler_running(void) { return scheduled; }
uint64_t scheduler_clock(void) { return test_clock; }
void task_sleep_ms(uint32_t ms) {
    CHECK(lock_depth == 0); sleeps++; test_clock += (uint64_t)ms * 1000000;
}
void rtc_get_time(rtc_time_t* t) { static uint32_t seconds; t->second = seconds++ % 60; }
uint8_t sys_net_is_ready(void) { return 1; }
uint64_t sys_net_get_mac_address(void) { return 0x123456789ABCull; }
void ipv4_set_configuration(uint32_t ip, uint32_t gateway, uint32_t mask) {
    test_ip = ip; test_gateway = gateway; test_mask = mask;
}
udp_socket_t* udp_connect(uint32_t ip, uint16_t port) {
    CHECK(test_ip == 0); CHECK(ip == 0xFFFFFFFF && port == 67);
    connected = 1; return &test_socket;
}
void udp_set_local_port(udp_socket_t* s, uint16_t port) { (void)s; CHECK(port == 68); }
void udp_bind(udp_socket_t* s, udp_data_handler_t fn) { (void)s; receiver = fn; }
void udp_disconnect(udp_socket_t* s) { (void)s; connected = 0; }
uint8_t udp_send(udp_socket_t* s, const uint8_t* p, uint16_t n) {
    (void)s; CHECK(n >= 244); CHECK(lock_depth > 0); outgoing_type = p[242]; return 1;
}
void sys_net_poll(void) {
    CHECK(lock_depth > 0);
    if (drop || !outgoing_type) return;
    uint8_t packet[260] = {0};
    packet[0] = 2; packet[1] = 1; packet[2] = 6;
    write_be32(packet + 4, transaction_id);
    write_be32(packet + 16, 0xC0A864E3);
    uint64_t mac = sys_net_get_mac_address();
    for (int i = 0; i < 6; i++) packet[28+i] = mac >> ((5-i)*8);
    packet[240] = 53; packet[241] = 1; packet[242] = outgoing_type == 1 ? 2 : 5;
    packet[243] = 1; packet[244] = 4; write_be32(packet + 245, 0xFFFFFF00);
    packet[249] = 54; packet[250] = 4; write_be32(packet + 251, 0xC0A86402);
    packet[255] = 255;
    receiver(&test_socket, 0xC0A86402, 67, packet, 256);
    CHECK(!response_received); /* Reject invalid DHCP cookie. */
    write_be32(packet + 236, DHCP_MAGIC);
    receiver(&test_socket, 0xC0A86402, 67, packet, 256);
    outgoing_type = 0;
}
int main(void) {
    dhcp_lease_t lease;
    test_ip = 0x0A0000E3; /* Renewal must not bind to the previous address. */
    CHECK(dhcp_request(&lease, 1));
    CHECK(lease.leased && test_ip == 0xC0A864E3 && test_mask == 0xFFFFFF00);
    CHECK(!connected && lock_depth == 0);
    drop = 1;
    CHECK(!dhcp_request(&lease, 1));
    CHECK(!test_ip && !test_mask && !test_gateway && !current_lease.leased);
    CHECK(sleeps >= 300 && !connected && lock_depth == 0);
    /* Boot-time timeout must work without attempting scheduler sleeps. */
    scheduled = 0; int old_sleeps = sleeps;
    CHECK(!dhcp_request(&lease, 1)); CHECK(sleeps == old_sleeps);
    CHECK(!test_ip && !connected && lock_depth == 0);
    drop = 0; scheduled = 1;
    CHECK(dhcp_request(&lease, 1)); /* A failed attempt must release request state. */
    dhcp_clear_lease(); CHECK(!dhcp_get_lease()->leased);
    return failures;
}
