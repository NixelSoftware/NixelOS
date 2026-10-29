#include "../../system/user/sys_api.h"

static void print(const char* s) {
    while (*s) {
        uint32_t len = 0;
        while (s[len]) len++;
        sys_write(s, len);
        s += len;
    }
}

static void print_hex4(uint16_t v) {
    const char hex[] = "0123456789abcdef";
    char b[5];
    b[0] = hex[(v >> 12) & 0xF];
    b[1] = hex[(v >> 8) & 0xF];
    b[2] = hex[(v >> 4) & 0xF];
    b[3] = hex[v & 0xF];
    b[4] = '\0';
    print(b);
}

static void print_hex2(uint8_t v) {
    const char hex[] = "0123456789abcdef";
    char b[3];
    b[0] = hex[(v >> 4) & 0xF];
    b[1] = hex[v & 0xF];
    b[2] = '\0';
    print(b);
}

static const char* pci_class_name(uint8_t cls, uint8_t sub) {
    switch (cls) {
        case 0x01: /* Mass Storage */
            if (sub == 0x01) return "IDE interface";
            if (sub == 0x06) return "SATA controller";
            return "Mass storage controller";
        case 0x02: /* Network */
            if (sub == 0x00) return "Ethernet controller";
            if (sub == 0x80) return "Network controller";
            return "Network controller";
        case 0x03: /* Display */
            if (sub == 0x00) return "VGA compatible controller";
            return "Display controller";
        case 0x04: /* Multimedia */
            if (sub == 0x01) return "Multimedia audio controller";
            if (sub == 0x03) return "Audio device";
            return "Multimedia controller";
        case 0x06: /* Bridge */
            if (sub == 0x00) return "Host bridge";
            if (sub == 0x01) return "ISA bridge";
            if (sub == 0x04) return "PCI bridge";
            return "Bridge";
        case 0x0C: /* Serial Bus */
            if (sub == 0x03) return "USB controller";
            if (sub == 0x05) return "SMBus";
            return "Serial bus controller";
        default:
            return "Unclassified device";
    }
}

static const char* pci_vendor_name(uint16_t vendor) {
    switch (vendor) {
        case 0x8086: return "Intel Corporation";
        case 0x10EC: return "Realtek Semiconductor Co., Ltd.";
        case 0x14E4: return "Broadcom Inc.";
        case 0x1022: return "Advanced Micro Devices, Inc. [AMD]";
        case 0x1234: return "QEMU Virtual Device";
        case 0x1AF4: return "Red Hat, Inc. Virtio";
        case 0x1013: return "Cirrus Logic";
        case 0x1000: return "LSI Logic / Symbios Logic";
        case 0x104B: return "BusLogic";
        case 0x106B: return "Apple Inc.";
        default:     return 0;
    }
}

void user_main(const char* args) {
    (void)args;
    user_pci_device_t dev;
    uint32_t idx = 0;

    while (sys_pci_device_info(idx, &dev) == 0) {
        /* BDF: 00:00.0 */
        print_hex2(dev.bus);
        print(":");
        print_hex2(dev.slot);
        print(".");
        print_hex2(dev.function);
        print(" ");

        /* Class Description */
        print(pci_class_name(dev.class_code, dev.subclass));
        print(": ");

        /* Vendor name or hex */
        const char* vname = pci_vendor_name(dev.vendor_id);
        if (vname) {
            print(vname);
            print(" ");
        }

        /* [vendor:device] */
        print("[");
        print_hex4(dev.vendor_id);
        print(":");
        print_hex4(dev.device_id);
        print("]");

        /* Revision & IRQ */
        if (dev.irq_line && dev.irq_line != 0xFF) {
            print(" (irq ");
            char num[4];
            int p = 0;
            uint8_t q = dev.irq_line;
            if (q >= 100) { num[p++] = '0' + (q / 100); q %= 100; }
            if (p || q >= 10) { num[p++] = '0' + (q / 10); q %= 10; }
            num[p++] = '0' + q;
            num[p] = '\0';
            print(num);
            print(")");
        }
        print("\n");

        idx++;
    }

    if (idx == 0) {
        print("No PCI devices reported by system.\n");
    }
}