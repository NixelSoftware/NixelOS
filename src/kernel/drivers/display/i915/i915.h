/* SPDX-License-Identifier: MIT */
#ifndef NIXEL_I915_H
#define NIXEL_I915_H
#include "../../../hal/video.h"
#define I915_SCANOUT_PAGES ((SCREEN_MAX_WIDTH * 4u * SCREEN_MAX_HEIGHT + 4095u) / 4096u)
/* Nixel services replace intel_de, GEM allocation and Linux polling helpers.
 * All entry points run under kernel_mutex, exclusively with presentation. */
typedef struct {
    void* context;
    uint32_t (*read)(void*, uint32_t);
    void (*write)(void*, uint32_t, uint32_t);
    void (*delay_us)(void*, uint32_t);
    uint32_t (*allocate)(void*, uint32_t);
    void (*release)(void*, uint32_t, uint32_t);
    void (*flush_cpu)(void*);
} i915_io_t;
typedef struct {
    i915_io_t io;
    uint32_t aperture, plane, pipe, panel_width, panel_height;
    uint32_t physical, gtt_offset, pages;
    uint32_t original_ptes[I915_SCANOUT_PAGES];
    int attached, modeset, fault;
} i915_display_t;
void i915_display_adopt(i915_display_t* dev, const i915_io_t* io,
                        uint32_t aperture, uint32_t plane, uint32_t pipe);
int i915_display_can_modeset(i915_display_t* dev);
int i915_display_set_mode(i915_display_t* dev, video_framebuffer_t* fb,
                          uint32_t width, uint32_t height);
uint32_t i915_panel_fitting(uint32_t pipe, uint32_t width, uint32_t height,
                            uint32_t panel_width, uint32_t panel_height);
#endif
