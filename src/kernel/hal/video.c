#include "video.h"
#include "../mutex.h"
#include "../drivers/display/video_drivers.h"
#include "../drivers/serial.h"

int32_t screen_width = SCREEN_DEFAULT_WIDTH, screen_height = SCREEN_DEFAULT_HEIGHT;
static video_framebuffer_t framebuffer;
static int virtio_active, qemu_active, intel_active;
static uint32_t requested_width, requested_height;
static int resolution_status;
/* Presentation transfers run outside kernel_mutex. Serialize the control queue
 * with 3D submissions without requiring the worker to reacquire kernel_mutex. */
static mutex_t command_mutex;
int video_init(uint32_t address, uint32_t bpp, uint32_t pitch) {
    framebuffer = (video_framebuffer_t){0};
    resolution_status = 0;
    virtio_active = virtio_gpu_init(&framebuffer);
    qemu_active = !virtio_active && qemu_video_init(&framebuffer);
    intel_active = !virtio_active && !qemu_active &&
        intel_gma_video_init(&framebuffer, address, bpp, pitch);
    if (!virtio_active && !qemu_active && !intel_active &&
        !vga_video_init(&framebuffer, address, bpp, pitch)) {
        debug_print("[VIDEO] No supported framebuffer\n");
        return 0;
    }
    debug_print("[VIDEO] Driver: ");
    debug_print(framebuffer.driver_name);
    debug_print("\n");
    return 1;
}
const video_framebuffer_t* video_get_framebuffer(void) {
    return framebuffer.address ? &framebuffer : 0;
}
void video_write_span(int32_t x, int32_t y, const uint32_t* pixels, uint32_t count) {
    if (!framebuffer.address || !pixels || y < 0 || y >= SCREEN_HEIGHT) return;
    if (x < 0) {
        uint32_t skip = 0u - (uint32_t)x;
        if (skip >= count) return;
        pixels += skip; count -= skip; x = 0;
    }
    if (x >= SCREEN_WIDTH) return;
    if (count > SCREEN_WIDTH - (uint32_t)x) count = SCREEN_WIDTH - (uint32_t)x;
    if (virtio_active && count) virtio_gpu_mark_dirty(x, y, count);
    uint8_t* dest = (uint8_t*)framebuffer.address + y * framebuffer.pitch +
                    x * (framebuffer.bpp / 8);
    if (framebuffer.bpp == 32) {
        __asm__ volatile ("cld; rep movsl" : "+D"(dest), "+S"(pixels), "+c"(count) : : "memory");
    } else {
        volatile uint8_t* out = dest;
        while (count--) {
            uint32_t color = *pixels++;
            *out++ = color; *out++ = color >> 8; *out++ = color >> 16;
        }
    }
}

int video_can_set_resolution(void) {
    return virtio_active || qemu_active || (intel_active && intel_gma_can_set_resolution());
}
int video_native_resolution(uint32_t* width, uint32_t* height) {
    return width && height && intel_active && intel_gma_native_resolution(width,height);
}
int video_request_resolution(uint32_t width, uint32_t height) {
    MUTEX_GUARD(kernel_mutex);
    if (!video_can_set_resolution()) { resolution_status = -1; return 0; }
    if (!framebuffer.address || width < 640 || height < 480 ||
        width > SCREEN_MAX_WIDTH || height > SCREEN_MAX_HEIGHT) return 0;
    requested_width = width; requested_height = height;
    resolution_status = 1;
    return 1;
}
int video_resolution_status(void) { return resolution_status; }
int video_poll_resize(void) {
    MUTEX_GUARD(command_mutex);
    if (resolution_status == 1) {
        int ok = virtio_active ? virtio_gpu_set_resolution(&framebuffer, requested_width, requested_height) :
            qemu_active ? qemu_video_set_resolution(&framebuffer, requested_width, requested_height) :
            (intel_active && intel_gma_set_resolution(&framebuffer, requested_width, requested_height));
        resolution_status = ok ? 2 : -1;
        return ok;
    }
    return virtio_active && virtio_gpu_poll_resize(&framebuffer);
}
void video_present(void) {
    MUTEX_GUARD(command_mutex);
    if (virtio_active) virtio_gpu_present();
}
int video_3d_available(void) {
    MUTEX_GUARD(command_mutex);
    return virtio_active && virgl_available();
}
const uint32_t* video_3d_render(const user_3d_triangle_t* triangles, uint32_t count,
    uint32_t width, uint32_t height, uint32_t clear_color, uint32_t* stride) {
    MUTEX_GUARD(command_mutex);
    if (!virtio_active) return 0;
    return virgl_render(triangles, count, width, height, clear_color, stride);
}
