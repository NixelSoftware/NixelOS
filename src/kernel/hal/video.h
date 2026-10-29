#ifndef NIXEL_HAL_VIDEO_H
#define NIXEL_HAL_VIDEO_H
#include "../types.h"

/* Drivers publish a packed BGR framebuffer; the renderer supplies XRGB pixels.
 * Geometry is published through SCREEN_WIDTH/SCREEN_HEIGHT under kernel_mutex. */
typedef struct {
    uint32_t address, bpp, pitch;
    const char* driver_name;
} video_framebuffer_t;

int video_init(uint32_t boot_address, uint32_t boot_bpp, uint32_t boot_pitch);
const video_framebuffer_t* video_get_framebuffer(void);
/* Queue changes for the presentation worker; geometry must not change mid-transfer. */
int video_can_set_resolution(void);
int video_native_resolution(uint32_t* width, uint32_t* height);
int video_request_resolution(uint32_t width, uint32_t height);
int video_resolution_status(void); /* 0 idle, 1 pending, 2 applied, -1 failed */
int video_poll_resize(void);
void video_present(void);
/* Caller serializes presentation; spans are clipped to the visible row. */
void video_write_span(int32_t x, int32_t y, const uint32_t* pixels, uint32_t count);
#endif
