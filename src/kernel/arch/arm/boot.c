#include "types.h"
#include "serial.h"
#include "mmu.h"
#include "gic.h"
#include "virtio_gpu.h"
#include "virtio_input.h"
#include "virtio_blk.h"
#include "arch/arch.h"
#include "task.h"

extern void kernel_main(const arch_boot_info_t* boot);

static void put_hex(uint32_t value) {
    static const char digits[] = "0123456789ABCDEF";
    virt_serial_write("0x");
    for (int shift = 28; shift >= 0; shift -= 4)
        virt_serial_putc(digits[(value >> shift) & 0xFu]);
}

__attribute__((noreturn)) void arm_exception(uint32_t vector, uint32_t lr) {
    uint32_t ifar, spsr;
    __asm__ volatile ("mrc p15, 0, %0, c6, c0, 2" : "=r"(ifar));
    __asm__ volatile ("mrs %0, spsr" : "=r"(spsr));
    virt_serial_write("\nEXCEPTION ");
    virt_serial_putc((char)('0' + vector));
    virt_serial_write(" lr=");
    put_hex(lr);
    virt_serial_write(" ifar=");
    put_hex(ifar);
    virt_serial_write(" spsr=");
    put_hex(spsr);
    if (current_task) {
        virt_serial_write(" task=");
        virt_serial_write(current_task->name);
    }
    virt_serial_write("\n");
    for (;;) __asm__ volatile ("wfi");
}

__attribute__((noreturn)) void arm_main(uint32_t dtb) {
    virt_serial_init();
    virt_serial_write("\nNixelOS ARM bring-up\n");
    virt_serial_write("DTB: ");
    put_hex(dtb);
    virt_serial_write("\n");

    arm_mmu_init();
    virt_serial_write("MMU: enabled, SCTLR=");
    put_hex(arm_mmu_sctlr());
    virt_serial_write("\n");

    int gpu = virtio_gpu_init(640, 480);
    int input = virtio_input_init();
    int blk = virtio_blk_init();
    virt_serial_write(gpu ? "GPU: virtio-gpu 640x480\n" : "GPU: not found\n");
    virt_serial_write(input ? "Input: virtio-keyboard\n" : "Input: none\n");

    if (blk) {
        static uint8_t mbr[512] __attribute__((aligned(512)));
        static uint8_t super[1024] __attribute__((aligned(512)));
        if (virtio_blk_read(0, mbr, 1) != 0 || mbr[510] != 0x55 || mbr[511] != 0xAA) {
            virt_serial_write("BLK: bad MBR\n");
        } else {
            uint32_t part_lba = (uint32_t)mbr[454] | ((uint32_t)mbr[455] << 8) |
                                ((uint32_t)mbr[456] << 16) | ((uint32_t)mbr[457] << 24);
            uint32_t part_sectors = (uint32_t)mbr[458] | ((uint32_t)mbr[459] << 8) |
                                    ((uint32_t)mbr[460] << 16) | ((uint32_t)mbr[461] << 24);
            virt_serial_write("BLK: virtio-blk, sectors=");
            put_hex((uint32_t)virtio_blk_sectors());
            virt_serial_write(" part0=");
            put_hex(part_lba);
            virt_serial_write("+");
            put_hex(part_sectors);
            virt_serial_write("\n");
            if (part_lba && virtio_blk_read(part_lba + 2u, super, 2) == 0) {
                char label[17];
                for (int i = 0; i < 16; i++) {
                    uint8_t ch = super[0x78 + i];
                    label[i] = (ch >= 0x20 && ch < 0x7F) ? (char)ch : '.';
                }
                label[16] = 0;
                int end = 16;
                while (end > 0 && label[end - 1] == '.') label[--end] = 0;
                virt_serial_write("BLK: ext4 magic=");
                put_hex((uint32_t)super[0x38] | ((uint32_t)super[0x39] << 8));
                virt_serial_write(" label=");
                virt_serial_write(label);
                virt_serial_write("\n");
            } else {
                virt_serial_write("BLK: superblock read failed\n");
            }
        }
    } else {
        virt_serial_write("BLK: not found\n");
    }

    arch_boot_info_t boot = {0};
    boot.fb_addr = (uint32_t)virtio_gpu_framebuffer();
    boot.fb_pitch = virtio_gpu_width() * 4u;
    boot.fb_bpp = 32;
    boot.fb_width = virtio_gpu_width();
    boot.fb_height = virtio_gpu_height();
    boot.region_count = 1;
    boot.regions[0].base = 0x40000000u;
    boot.regions[0].length = 0x10000000u;
    boot.regions[0].usable = 1;

    arm_gic_init();
    virt_serial_write("GIC: ready\n");
    virt_serial_write("Kernel: entering kernel_main\n");
    kernel_main(&boot);

    for (;;) __asm__ volatile ("wfi");
}
