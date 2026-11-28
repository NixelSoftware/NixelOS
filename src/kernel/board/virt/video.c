#include "types.h"
#include "hal/video.h"
#include "virtio_gpu.h"

int32_t screen_width = 640;
int32_t screen_height = 480;

static video_framebuffer_t framebuffer;
static int active;

int video_init(uint32_t boot_address, uint32_t boot_bpp, uint32_t boot_pitch) {
    (void)boot_address;
    (void)boot_bpp;
    (void)boot_pitch;
    uint32_t* ptr = virtio_gpu_framebuffer();
    if (!ptr) return 0;
    framebuffer.address = (uint32_t)ptr;
    framebuffer.bpp = 32;
    framebuffer.pitch = virtio_gpu_width() * 4u;
    framebuffer.driver_name = "virtio-gpu";
    screen_width = (int32_t)virtio_gpu_width();
    screen_height = (int32_t)virtio_gpu_height();
    active = 1;
    return 1;
}

const video_framebuffer_t* video_get_framebuffer(void) {
    return active ? &framebuffer : 0;
}

void video_write_span(int32_t x, int32_t y, const uint32_t* pixels, uint32_t count) {
    if (!active || !pixels || y < 0 || y >= SCREEN_HEIGHT) return;
    if (x < 0) {
        uint32_t skip = 0u - (uint32_t)x;
        if (skip >= count) return;
        pixels += skip;
        count -= skip;
        x = 0;
    }
    if (x >= SCREEN_WIDTH) return;
    if (count > (uint32_t)(SCREEN_WIDTH - x)) count = (uint32_t)(SCREEN_WIDTH - x);
    uint32_t* dest = (uint32_t*)(framebuffer.address + (uint32_t)y * framebuffer.pitch) + x;
    while (count--) *dest++ = *pixels++;
}

void video_present(void) {
    if (active) virtio_gpu_present();
}

int video_can_set_resolution(void) {
    return 0;
}

int video_native_resolution(uint32_t* width, uint32_t* height) {
    (void)width;
    (void)height;
    return 0;
}

int video_request_resolution(uint32_t width, uint32_t height) {
    (void)width;
    (void)height;
    return 0;
}

int video_resolution_status(void) {
    return 0;
}

int video_poll_resize(void) {
    return 0;
}

int video_3d_available(void) {
    return 0;
}

const uint32_t* video_3d_render(const user_3d_triangle_t* triangles, uint32_t count,
    uint32_t width, uint32_t height, uint32_t clear_color, uint32_t* stride) {
    (void)triangles;
    (void)count;
    (void)width;
    (void)height;
    (void)clear_color;
    (void)stride;
    return 0;
}

const char* video_gpu_diagnose(int run) {
    (void)run;
    return "virtio-gpu on QEMU virt.\n";
}
