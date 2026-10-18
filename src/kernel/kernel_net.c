#include "kernel_net.h"
#include "drivers/net.h"
#include "drivers/net_e1000.h"

uint32_t net_driver_type = 1;

uint8_t sys_net_init(uint16_t io) {
    if (net_driver_type == 1) {
        return e1000_init();
    }
    return net_init(io);
}

uint8_t sys_net_is_ready(void) {
    if (net_driver_type == 1) {
        return e1000_is_ready();
    }
    return net_is_ready();
}

void sys_net_shutdown(void) {
    if (net_driver_type == 1) {
        e1000_shutdown();
        return;
    }
    net_shutdown();
}

void sys_net_poll(void) {
    if (net_driver_type == 1) {
        e1000_poll();
        return;
    }
    net_poll();
}

uint8_t sys_net_send(const uint8_t* data, uint32_t length) {
    if (net_driver_type == 1) {
        return e1000_send(data, length);
    }
    return net_send(data, length);
}

void sys_net_set_receive_handler(net_receive_handler_t handler) {
    if (net_driver_type == 1) {
        e1000_set_receive_handler(handler);
        return;
    }
    net_set_receive_handler(handler);
}

uint64_t sys_net_get_mac_address(void) {
    if (net_driver_type == 1) {
        return e1000_get_mac_address();
    }
    return net_get_mac_address();
}

void sys_net_set_ip_address(uint32_t address) {
    if (net_driver_type == 1) {
        e1000_set_ip_address(address);
        return;
    }
    net_set_ip_address(address);
}

uint32_t sys_net_get_ip_address(void) {
    if (net_driver_type == 1) {
        return e1000_get_ip_address();
    }
    return net_get_ip_address();
}

void sys_net_get_diagnostics(net_diagnostics_t* d) {
    if (net_driver_type == 1) {
        e1000_get_diagnostics(d);
        return;
    }
    net_get_diagnostics(d);
}