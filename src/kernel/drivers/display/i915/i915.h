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
    void (*flush_line)(void*, const void*); /* clflush: make GPU writes visible to the CPU */
    /* Optional diagnostics sink for machines without a serial port. Called before
     * each risky step; text beginning "FAIL" marks the step that went wrong. */
    void (*trace)(void*, const char* text, uint32_t value, int has_value);
} i915_io_t;
/* Gen4 family (965/G35/GM965/GM45/G45...). g4x selects the GM45/G45 generation
 * (gen4.5), whose display watermarks and 3D packets differ from original gen4. */
typedef struct { uint16_t id; uint8_t g4x, mobile; const char* name; } i915_gen4_info_t;
const i915_gen4_info_t* i915_gen4_lookup(uint16_t device_id);
typedef struct {
    i915_io_t io;
    uint32_t aperture, plane, pipe, panel_width, panel_height;
    uint32_t physical, gtt_offset, pages;
    uint32_t original_ptes[I915_SCANOUT_PAGES];
    const i915_gen4_info_t* info;
    int attached, modeset, fault;
} i915_display_t;
int i915_gtt_reserve(i915_display_t* dev, uint32_t pages, uint32_t* gtt_offset,
                     uint32_t* physical, uint32_t* saved_ptes);
int i915_gtt_can_reserve(i915_display_t* dev, uint32_t pages);
void i915_gtt_unreserve(i915_display_t* dev, uint32_t gtt_offset, uint32_t physical,
                        uint32_t pages, const uint32_t* saved_ptes);
void i915_display_adopt(i915_display_t* dev, const i915_io_t* io,
                        const i915_gen4_info_t* info, uint32_t aperture, uint32_t plane, uint32_t pipe);
int i915_display_can_modeset(i915_display_t* dev);
int i915_display_set_mode(i915_display_t* dev, video_framebuffer_t* fb,
                          uint32_t width, uint32_t height);
uint32_t i915_panel_fitting(uint32_t pipe, uint32_t width, uint32_t height,
                            uint32_t panel_width, uint32_t panel_height);
#endif
