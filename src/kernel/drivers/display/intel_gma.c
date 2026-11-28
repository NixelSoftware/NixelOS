#include "video_drivers.h"
#include "i915/i915.h"
#include "i915/gen4_engine.h"
#include "../delay.h"
#include "../../arch/arch.h"
#include "../../mm.h"
#include "../pci.h"
#include "../serial.h"

/* PCI/firmware handoff glue for the Nixel i915 gen4 display port. The module
 * in i915/ adapts the supplied upstream display routines to Nixel services. */
static i915_display_t intel_display;
static gen4_engine_t intel_engine;
static int engine_probed;
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
    arch_cache_flush();
}
static void native_flush_line(void* context, const void* address) {
    (void)context;
    __asm__ volatile ("clflush (%0)" :: "r"(address) : "memory");
}
/* ---- Diagnostics for machines without a serial port ----------------------
 * Text goes to a RAM buffer (shown by the gpudiag shell command and saved to a
 * file) and to the kernel log. Progress also paints one cell per step straight
 * into the scanout memory, so if the GPU hangs the machine the last lit cell
 * says how far bring-up got: white = in progress, green = done, red = failed. */
#define DIAG_BYTES 3072u
static char diag[DIAG_BYTES];
static uint32_t diag_length, diag_step;
static int diag_failed_cell = -1;
static void diag_put(const char* text) {
    while (*text && diag_length < DIAG_BYTES - 1u) diag[diag_length++] = *text++;
    diag[diag_length] = 0;
}
static void diag_hex(uint32_t value) {
    static const char digits[] = "0123456789ABCDEF";
    char text[11] = {'0','x'};
    for (int i = 0; i < 8; i++) text[2 + i] = digits[(value >> (28 - i * 4)) & 15u];
    text[10] = 0;
    diag_put(text);
}
static void paint_cell(uint32_t index, uint32_t color) {
    const video_framebuffer_t* fb = video_get_framebuffer();
    if (!fb || fb->bpp != 32 || index >= 40u) return;
    uint32_t x0 = 8u + index * 16u, y0 = (uint32_t)SCREEN_HEIGHT - 20u;
    if (x0 + 12u > (uint32_t)SCREEN_WIDTH || y0 + 12u > (uint32_t)SCREEN_HEIGHT) return;
    for (uint32_t y = 0; y < 12u; y++) {
        volatile uint32_t* row = (volatile uint32_t*)(unsigned long)
            (fb->address + (y0 + y) * fb->pitch + x0 * 4u);
        for (uint32_t x = 0; x < 12u; x++) row[x] = color;
    }
    /* Make sure the cell reaches the display before the next risky write. */
    arch_cache_flush();
}
/* Tiny 3x5 hex font so failure registers can be read off the screen even if
 * the machine dies right afterwards. Rows (top-left, newest below): see
 * documentation/gpudiag.txt for the order. */
static const uint8_t hex_font[16][5] = {
    {7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,7,1,7},{5,5,7,1,1},{7,4,7,1,7},
    {7,4,7,5,7},{7,1,1,1,1},{7,5,7,5,7},{7,5,7,1,7},{7,5,7,5,5},{6,5,6,5,6},
    {7,4,4,4,7},{6,5,5,5,6},{7,4,6,4,7},{7,4,6,4,4}};
static uint32_t dump_row;
static int dump_active;
static void paint_hex_row(uint32_t row, uint32_t value) {
    const video_framebuffer_t* fb = video_get_framebuffer();
    if (!fb || fb->bpp != 32 || row >= 24u) return;
    uint32_t x0 = 8u, y0 = 8u + row * 18u;
    if (y0 + 16u > (uint32_t)SCREEN_HEIGHT || x0 + 100u > (uint32_t)SCREEN_WIDTH) return;
    for (uint32_t y = 0; y < 16u; y++) {
        volatile uint32_t* line = (volatile uint32_t*)(unsigned long)
            (fb->address + (y0 + y) * fb->pitch + x0 * 4u);
        for (uint32_t x = 0; x < 100u; x++) line[x] = 0;
    }
    for (uint32_t digit = 0; digit < 8u; digit++) {
        uint32_t nibble = (value >> (28u - digit * 4u)) & 15u;
        for (uint32_t gy = 0; gy < 5u; gy++) for (uint32_t gx = 0; gx < 3u; gx++) {
            if (!(hex_font[nibble][gy] & (4u >> gx))) continue;
            for (uint32_t py = 0; py < 3u; py++) for (uint32_t px = 0; px < 3u; px++) {
                volatile uint32_t* dot = (volatile uint32_t*)(unsigned long)
                    (fb->address + (y0 + gy * 3u + py) * fb->pitch +
                     (x0 + digit * 12u + gx * 3u + px) * 4u);
                *dot = 0x00FFFFFFu;
            }
        }
    }
    arch_cache_flush();
}
static void diag_trace(void* context, const char* text, uint32_t value, int has_value) {
    (void)context;
    int fail = text[0] == 'F' && text[1] == 'A' && text[2] == 'I' && text[3] == 'L';
    int step = text[0] == 's' && text[1] == 't' && text[2] == 'e' && text[3] == 'p';
    if (step) {
        if (diag_step && (int)(diag_step - 1u) != diag_failed_cell)
            paint_cell(diag_step - 1u, 0x0000C000u);
        paint_cell(diag_step++, 0x00FFFFFFu);
    } else if (fail) {
        if (diag_step) { diag_failed_cell = (int)diag_step - 1; paint_cell(diag_step - 1u, 0x00FF0000u); }
        dump_active = 1; dump_row = 0;
    }
    if (dump_active && has_value) paint_hex_row(dump_row++, value);
    diag_put(text);
    if (has_value) diag_hex(value);
    diag_put("\n");
    debug_print("[GPU] "); debug_print(text);
    if (has_value) debug_hex(value);
    debug_print("\n");
}
static uint32_t intel_can_modeset_flag(void) { return i915_display_can_modeset(&intel_display); }
static void diag_line(const char* text, uint32_t value) { diag_trace(0, text, value, 1); }
/* run == 0: read-only survey. run == 1: also bring up the command streamer and
 * execute a batch (writes GPU registers; can hang a machine whose silicon
 * disagrees with the port). Returns the report text. */
const char* intel_gma_diagnose(int run) {
    if (run < 0) return diag; /* last report, unchanged */
    diag_length = 0; diag[0] = 0; diag_step = 0; dump_active = 0; dump_row = 0; diag_failed_cell = -1;
    if (!intel_display.attached || !intel_display.info) {
        diag_put("No supported Intel gen4 display adapter was adopted at boot.\n");
        return diag;
    }
    intel_display.io.trace = diag_trace;
    diag_put("Intel gen4: "); diag_put(intel_display.info->name);
    diag_put(intel_display.info->g4x ? " (G4X, gen4.5)\n" : " (gen4)\n");
    diag_line("display modeset capable ", intel_can_modeset_flag());
    diag_line("panel width ", intel_display.panel_width);
    diag_line("panel height ", intel_display.panel_height);
    diag_line("pipe ", intel_display.pipe); diag_line("plane ", intel_display.plane);
    diag_line("GGTT 4-page range available ", i915_gtt_can_reserve(&intel_display, GEN4_ENGINE_PAGES));
    diag_line("GGTT scanout range available ",
              intel_display.physical ? 1u : i915_gtt_can_reserve(&intel_display, I915_SCANOUT_PAGES));
    gen4_engine_report(&intel_display);
    if (!run) {
        diag_put("Read-only survey done. Run 'gpudiag run' to test the engine.\n");
        intel_display.io.trace = 0; return diag;
    }
    if (engine_probed) {
        diag_put("Engine bring-up already attempted this boot; reboot to retry.\n");
        intel_display.io.trace = 0; return diag;
    }
    engine_probed = 1;
    /* Cell 21 is the verdict, painted before anything else can go wrong:
     * green = selftest passed, red = failed, yellow = ring init declined. */
    if (!gen4_engine_init(&intel_engine, &intel_display)) {
        paint_cell(20, 0x00FFFF00u); diag_put("RESULT: ring init declined\n");
    } else if (!gen4_engine_selftest(&intel_engine)) {
        paint_cell(20, 0x00FF0000u);
        diag_put("RESULT: selftest FAILED\n"); gen4_engine_shutdown(&intel_engine);
    } else {
        paint_cell(20, 0x0000FF00u);
        diag_put("RESULT: command streamer OK\n"); gen4_engine_shutdown(&intel_engine);
    }
    intel_display.io.trace = 0;
    return diag;
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
    return dev && dev->vendor_id == 0x8086 && i915_gen4_lookup(dev->device_id) &&
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
                        native_delay,native_allocate,native_release,native_flush,
                        native_flush_line};
        i915_display_adopt(&intel_display,&io,i915_gen4_lookup(dev->device_id),base,plane,!!(control & GMA_PIPE_B));
        *fb = (video_framebuffer_t){address, 32, pitch, "Nixel i915 (gen4)"};
        debug_print("[VIDEO] Intel gen4 graphics: firmware scanout validated\n");
        return 1;
    }
    return 0;
}
