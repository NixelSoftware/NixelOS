/* cc -I. -ffunction-sections -fdata-sections tests/settings_network_test.c
 *    -Wl,--gc-sections -o /tmp/settings-network-test && /tmp/settings-network-test */
#include "src/system/graphics/settings/settings.c"
static int calls, failures;
static uint64_t test_clock;
uint64_t scheduler_clock(void) { return test_clock; }
int video_resolution_status(void) { return 0; }
static uint32_t applied_ip, applied_mask, applied_gateway;
#define CHECK(condition) do { if (!(condition)) failures++; } while (0)
void request_redraw(void) {}
uint8_t sys_net_set_manual_configuration(uint32_t ip, uint32_t mask, uint32_t gateway) {
    calls++; applied_ip = ip; applied_mask = mask; applied_gateway = gateway; return 1;
}
static void copy(char* dest, const char* source) { while ((*dest++ = *source++)) {} }
int main(void) {
    uint32_t ip;
    CHECK(parse_ip("192.168.100.227", &ip) && ip == 0xC0A864E3);
    CHECK(parse_ip("0.0.0.0", &ip) && ip == 0);
    const char* invalid[] = {"", "1.2.3", "1.2.3.4.5", "256.1.1.1", "1..2.3", "1.2.3.4x", "-1.2.3.4", "1234.2.3.4"};
    for (int i = 0; i < 8; i++) CHECK(!parse_ip(invalid[i], &ip));
    copy(manual[0], "192.168.100.227"); copy(manual[1], "255.255.255.0"); copy(manual[2], "192.168.100.2");
    apply_manual(); CHECK(calls == 1 && applied_ip == 0xC0A864E3 && applied_mask == 0xFFFFFF00 && applied_gateway == 0xC0A86402);
    copy(manual[0], "999.1.1.1"); apply_manual(); CHECK(calls == 1);
    busy = 1; copy(manual[0], "1.2.3.4"); apply_manual(); CHECK(calls == 1);
    busy = 0; tab = 1; field = 0; replace_field = 1;
    key(&window, '2'); CHECK(manual[0][0] == '2' && manual[0][1] == 0);
    key(&window, '\b'); CHECK(!manual[0][0]); key(&window, '\t'); CHECK(field == 1);
    set_message("Output device changed.");
    test_clock = 2999999999ull; settings_poll(); CHECK(message_expires != 0);
    /* A subsequent notification restarts the timeout. */
    set_message("Output device changed.");
    test_clock = 3000000000ull; settings_poll(); CHECK(message_expires != 0);
    test_clock = 5999999999ull; settings_poll(); CHECK(message_expires == 0);
    CHECK(message[0] == 'N');
    return failures;
}
