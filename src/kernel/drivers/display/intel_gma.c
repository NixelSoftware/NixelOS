#include "video_drivers.h"
#include "i915/i915.h"
#include "../delay.h"
#include "../../paging.h"
#include "../pci.h"
#include "../serial.h"

/* PCI/firmware handoff glue for the Nixel i915 gen4 display port. The module
 * in i915/ adapts the supplied upstream display routines to Nixel services. */
static i915_display_t intel_display;
#define GMA_MMIO_BYTES       0x00100000u
#define GMA_APERTURE_MIN     0x08000000u /* Common lower window of 128/256/512 MiB apertures. */
#define GMA_GTT_OFFSET       0x00080000u
#define GMA_PGTBL_CTL        0x00002020u
#define GMA_PLANE_CONTROL    0x00070180u
#define GMA_PLANE_LINEAR     0x00070184u
#define GMA_PLANE_STRIDE     0x00070188u
#define GMA_PLANE_SURFACE    0x0007019Cu
#define GMA_PIPE_CONFIG      0x00070008u
#define GMA_PIPE_SOURCE      0x0006001Cu
#define GMA_SECOND           0x00001000u
#define GMA_ENABLED          (1u << 31)
#define GMA_FORMAT_MASK      (15u << 26)
#define GMA_BGRX8888         (6u << 26)
#define GMA_TILED            (1u << 10)
#define GMA_ROTATED          (1u << 15)
#define GMA_PIPE_B           (1u << 24)

static uint32_t read_reg(uint32_t mmio, uint32_t offset) {
    return *(volatile uint32_t*)(unsigned long)(mmio + offset);
}
static uint32_t native_read(void* context, uint32_t offset) {
    return read_reg((uint32_t)(unsigned long)context,offset);
}
static void native_write(void* context, uint32_t offset, uint32_t value) {
    *(volatile uint32_t*)((unsigned long)context + offset) = value;
}
static void native_delay(void* context, uint32_t us) { (void)context; driver_delay_us(us); }
static uint32_t native_allocate(void* context, uint32_t pages) {
    (void)context; return paging_alloc_contiguous(pages);
}
static void native_release(void* context, uint32_t physical, uint32_t pages) {
    (void)context;
    for (uint32_t i = 0; i < pages; i++) paging_frame_free(physical + i * PAGE_SIZE);
}
static void native_flush(void* context) {
    (void)context;
    __asm__ volatile ("wbinvd" ::: "memory");
}
int intel_gma_can_set_resolution(void) { return i915_display_can_modeset(&intel_display); }
int intel_gma_native_resolution(uint32_t* width, uint32_t* height) {
    if (!intel_gma_can_set_resolution()) return 0;
    *width = intel_display.panel_width; *height = intel_display.panel_height;
    return *width <= SCREEN_MAX_WIDTH && *height <= SCREEN_MAX_HEIGHT;
}
int intel_gma_set_resolution(video_framebuffer_t* fb, uint32_t width, uint32_t height) {
    if (!i915_display_set_mode(&intel_display,fb,width,height)) return 0;
    screen_width = width; screen_height = height;
    debug_print("[VIDEO] i915 LVDS source mode changed\n");
    return 1;
}
static int supported(const pci_device_t* dev) {
    return dev && dev->vendor_id == 0x8086 &&
        (dev->device_id == 0x2A02 || dev->device_id == 0x2A12) &&
        dev->class_code == 3 && dev->subclass == 0 && dev->function == 0;
}
static int plane_matches(uint32_t mmio, uint32_t aperture, uint32_t address,
                          uint32_t pitch, uint32_t plane) {
    uint32_t delta = plane * GMA_SECOND;
    uint32_t control = read_reg(mmio, GMA_PLANE_CONTROL + delta);
    if (!(control & GMA_ENABLED) || (control & GMA_FORMAT_MASK) != GMA_BGRX8888 ||
        (control & (GMA_TILED | GMA_ROTATED))) return 0;
    uint32_t pipe_delta = (control & GMA_PIPE_B) ? GMA_SECOND : 0;
    if (!(read_reg(mmio, GMA_PIPE_CONFIG + pipe_delta) & GMA_ENABLED)) return 0;
    uint32_t source = read_reg(mmio, GMA_PIPE_SOURCE + pipe_delta);
    if ((source >> 16) + 1 != (uint32_t)SCREEN_WIDTH ||
        (source & 0xFFFFu) + 1 != (uint32_t)SCREEN_HEIGHT) return 0;
    if (read_reg(mmio, GMA_PLANE_STRIDE + delta) != pitch) return 0;
    uint32_t surface = read_reg(mmio, GMA_PLANE_SURFACE + delta) & 0xFFFFF000u;
    uint32_t linear = read_reg(mmio, GMA_PLANE_LINEAR + delta);
    return (uint64_t)aperture + surface + linear == address;
}
int intel_gma_video_init(video_framebuffer_t* fb, uint32_t address,
                          uint32_t bpp, uint32_t pitch) {
    if (!fb || !address || bpp != 32 || SCREEN_WIDTH <= 0 || SCREEN_HEIGHT <= 0 ||
        SCREEN_WIDTH > SCREEN_MAX_WIDTH || SCREEN_HEIGHT > SCREEN_MAX_HEIGHT ||
        pitch < (uint32_t)SCREEN_WIDTH * 4u || (pitch & 63u)) return 0;
    pci_init();
    for (uint32_t i = 0; i < pci_device_count(); i++) {
        const pci_device_t* dev = pci_device_at(i);
        if (!supported(dev)) continue;
        /* Use only the already enabled primary display, never wake or reset an
         * inactive integrated adapter in a system booted on another GPU. */
        if (!(pci_read16(dev, 4) & PCI_COMMAND_MEMORY)) continue;
        pci_bar_t mmio, aperture;
        if (!pci_get_bar(dev, 0, &mmio) || !pci_get_bar(dev, 2, &aperture) ||
            mmio.io || aperture.io || !mmio.address || !aperture.address ||
            (mmio.address & (GMA_MMIO_BYTES - 1u)) ||
            (aperture.address & (GMA_APERTURE_MIN - 1u)) ||
            mmio.address > 0x100000000ull - GMA_MMIO_BYTES ||
            aperture.address > 0x100000000ull - GMA_APERTURE_MIN) continue;
        uint64_t end = (uint64_t)address + (uint64_t)pitch * (SCREEN_HEIGHT - 1u) + SCREEN_WIDTH * 4u;
        if (address < aperture.address || end > aperture.address + GMA_APERTURE_MIN) continue;
        uint32_t registers = (uint32_t)mmio.address, base = (uint32_t)aperture.address;
        if (!(read_reg(registers, GMA_PGTBL_CTL) & 1u) ||
            (!plane_matches(registers, base, address, pitch, 0) &&
             !plane_matches(registers, base, address, pitch, 1))) continue;
        /* CPU aperture writes require valid GGTT entries, including pitch gaps.
         * Verify mappings without allocating or replacing firmware-owned pages. */
        uint32_t first = (address - base) / 4096u;
        uint32_t last = (uint32_t)(end - 1u - base) / 4096u;
        uint32_t page = first;
        while (page <= last && (read_reg(registers, GMA_GTT_OFFSET + page * 4u) & 1u)) page++;
        if (page <= last) continue;
        uint32_t plane = plane_matches(registers,base,address,pitch,0) ? 0 : 1;
        uint32_t control = read_reg(registers,GMA_PLANE_CONTROL + plane * GMA_SECOND);
        i915_io_t io = {(void*)(unsigned long)registers,native_read,native_write,
                        native_delay,native_allocate,native_release,native_flush};
        i915_display_adopt(&intel_display,&io,base,plane,!!(control & GMA_PIPE_B));
        *fb = (video_framebuffer_t){address, 32, pitch, "Nixel i915 / GMA X3100"};
        debug_print("[VIDEO] Mobile Intel 965 / GMA X3100: firmware scanout validated\n");
        return 1;
    }
    return 0;
}
