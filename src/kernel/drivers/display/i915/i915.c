// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020, 2023-2025 Intel Corporation
 * Copyright © 2006-2007 Intel Corporation
 * Copyright (c) 2006 Dave Airlie <airlied@linux.ie>
 *
 * Nixel gen4 display port. Adapted from i915 i9xx_plane.c, intel_pfit.c,
 * intel_lvds.c, intel_display.c and i9xx_wm.c. See COPYING and PORT.md.
 */
#include "i915.h"
#include "i915_regs.h"

static uint32_t rd(i915_display_t* d, uint32_t reg) { return d->io.read(d->io.context, reg); }
static void wr(i915_display_t* d, uint32_t reg, uint32_t value) {
    d->io.write(d->io.context, reg, value);
    (void)rd(d, reg); /* posting read before dependent operations */
}
static uint32_t preg(i915_display_t* d, uint32_t reg) { return reg + d->pipe * I915_STRIDE; }
static uint32_t plane_reg(i915_display_t* d, uint32_t reg) { return reg + d->plane * I915_STRIDE; }
static int wait_bits(i915_display_t* d, uint32_t reg, uint32_t mask,
                      uint32_t value, uint32_t milliseconds) {
    for (uint32_t n = 0; n < milliseconds * 10u; n++) {
        if ((rd(d, reg) & mask) == value) return 1;
        d->io.delay_us(d->io.context, 100);
    }
    return (rd(d, reg) & mask) == value;
}
/* Port of i965_scale_aspect(): keep the panel's fixed link timings and use
 * gen4's automatic aspect fitter. Like upstream, reject downscaling. */
uint32_t i915_panel_fitting(uint32_t pipe, uint32_t w, uint32_t h,
                            uint32_t panel_w, uint32_t panel_h) {
    uint32_t fit = pipe << 29;
    if (panel_w * h > w * panel_h) fit |= I915_ENABLE | I915_PFIT_PILLAR;
    else if (panel_w * h < w * panel_h) fit |= I915_ENABLE | I915_PFIT_LETTER;
    else if (panel_w != w) fit |= I915_ENABLE;
    return fit;
}
/* Only take over a single active, progressive LVDS output. No guessing at
 * CRT/SDVO timings, active GPU rings, compression or another pipe's scaler. */
int i915_display_can_modeset(i915_display_t* d) {
    if (!d || !d->attached || !d->modeset || d->fault) return 0;
    if ((rd(d,preg(d,I915_PIPECONF)) & (I915_ENABLE | I915_PIPE_STATE)) !=
        (I915_ENABLE | I915_PIPE_STATE)) return 0;
    uint32_t lvds = rd(d, I915_LVDS);
    uint32_t fit = rd(d, I915_PFIT_CONTROL);
    if (!(lvds & I915_ENABLE) || !!(lvds & I915_LVDS_PIPE_B) != d->pipe ||
        (rd(d, preg(d, I915_PIPECONF)) & (7u << 21)) ||
        (rd(d, I915_PIPECONF + (1u - d->pipe) * I915_STRIDE) & I915_ENABLE) ||
        (rd(d, I915_DSPCNTR + (1u - d->plane) * I915_STRIDE) & I915_ENABLE) ||
        ((fit & I915_ENABLE) && ((fit >> 29) & 3u) != d->pipe) ||
        (rd(d, I915_FBC_CONTROL) & I915_ENABLE) ||
        (rd(d, I915_CURSOR_CONTROL) & 0x3Fu) ||
        (rd(d, I915_CURSOR_CONTROL + I915_STRIDE) & 0x3Fu) ||
        (rd(d, I915_ADPA) & I915_ENABLE) ||
        (rd(d, I915_SDVOB) & I915_ENABLE) ||
        (rd(d, I915_SDVOC) & I915_ENABLE) ||
        (rd(d, 0x203Cu) & 1u) || (rd(d, 0x303Cu) & 1u) ||
        (rd(d, 0x403Cu) & 1u)) return 0;
    return (rd(d, I915_PP_STATUS) & (I915_ENABLE | I915_PP_SEQUENCE)) == I915_ENABLE;
}
void i915_display_adopt(i915_display_t* d, const i915_io_t* io,
                        uint32_t aperture, uint32_t plane, uint32_t pipe) {
    *d = (i915_display_t){0};
    d->io = *io; d->aperture = aperture; d->plane = plane; d->pipe = pipe;
    d->attached = 1;
    d->panel_width = (rd(d, preg(d, I915_HTOTAL)) & 0xFFFFu) + 1;
    d->panel_height = (rd(d, preg(d, I915_VTOTAL)) & 0xFFFFu) + 1;
    d->modeset = d->panel_width >= 640 && d->panel_height >= 480 &&
        d->panel_width <= 4096 && d->panel_height <= 4096;
}
/* Reserve a driver-owned GGTT range. Prefer invalid entries. Firmware often
 * fills the rest with one repeated scratch-page PTE; with engines/other planes
 * inactive, such aliases can also be reclaimed outside the live framebuffer.
 * Distinct valid mappings are never overwritten. Preserve original PTEs for
 * failed publication. Gen4 snoops writes through the GTT MMIO alias. */
static int pin_scanout(i915_display_t* d) {
    if (d->physical) return 1;
    uint32_t src = rd(d,preg(d,I915_PIPESRC));
    uint32_t live = (rd(d,plane_reg(d,I915_DSPSURF)) & 0xFFFFF000u) +
                    rd(d,plane_reg(d,I915_DSPLINOFF));
    uint64_t live_end = (uint64_t)live +
        (uint64_t)rd(d,plane_reg(d,I915_DSPSTRIDE)) * (src & 0xFFFFu) +
        ((src >> 16) + 1u) * 4u;
    if (live_end > 0x08000000u) return 0;
    uint32_t live_first = live / 4096u, live_last = (uint32_t)(live_end - 1u) / 4096u;
    uint32_t first = 0, found = 0;
    for (uint32_t pass = 0; pass < 2 && !found; pass++) {
        uint32_t run = 0, previous = 0;
        for (uint32_t i = 0; i < I915_GTT_ENTRIES; i++) {
            uint32_t pte = rd(d,I915_GTT + i * 4u);
            if (i >= live_first && i <= live_last) { run = 0; continue; }
            if (!pass) run = (pte & 1u) ? 0 : run + 1;
            else run = (pte & 1u) ? (run && pte == previous ? run + 1 : 1) : 0;
            previous = pte;
            if (run == I915_SCANOUT_PAGES) { first = i + 1 - run; found = 1; break; }
        }
    }
    if (!found) return 0;
    uint32_t physical = d->io.allocate(d->io.context, I915_SCANOUT_PAGES);
    if (!physical) return 0;
    d->io.flush_cpu(d->io.context); /* allocator zeros RAM through its WB alias */
    uint32_t i;
    for (i = 0; i < I915_SCANOUT_PAGES; i++) {
        uint32_t reg = I915_GTT + (first + i) * 4u;
        d->original_ptes[i] = rd(d, reg);
        uint32_t pte = (physical + i * 4096u) | 1u; /* uncached, valid */
        wr(d, reg, pte);
        if (rd(d, reg) != pte) break;
    }
    if (i != I915_SCANOUT_PAGES) {
        for (uint32_t j = 0; j <= i; j++) wr(d, I915_GTT + (first + j) * 4u, d->original_ptes[j]);
        d->io.release(d->io.context, physical, I915_SCANOUT_PAGES);
        return 0;
    }
    d->physical = physical; d->gtt_offset = first * 4096u; d->pages = I915_SCANOUT_PAGES;
    return 1;
}
/* Registers modified by the transaction. Immutable panel timings/PLL and
 * power-sequencer delays stay firmware-owned. */
enum { CONTROL, SURFACE, LINEAR, TILE, STRIDE, PIPE, SOURCE, FIT, RATIOS,
       BORDER, LVDS, POWER, PWM, SR, WM1, WM2, WM3, SNAP_COUNT };
static void snapshot_regs(i915_display_t* d, uint32_t* regs) {
    uint32_t list[SNAP_COUNT] = {
        plane_reg(d,I915_DSPCNTR), plane_reg(d,I915_DSPSURF), plane_reg(d,I915_DSPLINOFF),
        plane_reg(d,I915_DSPTILEOFF), plane_reg(d,I915_DSPSTRIDE), preg(d,I915_PIPECONF),
        preg(d,I915_PIPESRC), I915_PFIT_CONTROL, I915_PFIT_RATIOS, preg(d,I915_BCLRPAT),
        I915_LVDS, I915_PP_CONTROL, I915_PWM_CONTROL, I915_FW_BLC_SELF,
        I915_DSPFW1, I915_DSPFW2, I915_DSPFW3
    };
    for (int i = 0; i < SNAP_COUNT; i++) regs[i] = list[i];
}
static int panel_off(i915_display_t* d, const uint32_t* old) {
    wr(d, I915_PWM_CONTROL, old[PWM] & ~I915_ENABLE);
    wr(d, I915_PP_CONTROL, I915_PANEL_UNLOCK | (old[POWER] & 0xFFFFu & ~I915_PANEL_ON));
    if (!wait_bits(d,I915_PP_STATUS,I915_ENABLE | I915_PP_SEQUENCE,0,1000)) return 0;
    wr(d, I915_LVDS, old[LVDS] & ~I915_ENABLE);
    return 1;
}
static int pipe_off(i915_display_t* d, const uint32_t* old) {
    wr(d, plane_reg(d,I915_DSPCNTR), old[CONTROL] & ~I915_ENABLE);
    wr(d, plane_reg(d,I915_DSPSURF), old[SURFACE]);
    /* Plane changes latch at vblank. Wait for two scanline wraps unless
     * the pipe has already stopped (eg. rollback after a failed start). */
    if (rd(d,preg(d,I915_PIPECONF)) & I915_PIPE_STATE) {
        uint32_t previous = rd(d,preg(d,I915_PIPEDSL)) & 0x1FFFu;
        int wraps = 0;
        for (int i = 0; i < 1000 && wraps < 2; i++) {
            d->io.delay_us(d->io.context,100);
            uint32_t line = rd(d,preg(d,I915_PIPEDSL)) & 0x1FFFu;
            if (line < previous) wraps++;
            previous = line;
        }
        if (wraps != 2) return 0;
    }
    wr(d,preg(d,I915_PIPECONF),old[PIPE] & ~I915_ENABLE);
    return wait_bits(d,preg(d,I915_PIPECONF),I915_PIPE_STATE,0,100);
}
static int panel_on(i915_display_t* d, const uint32_t* old) {
    wr(d,I915_LVDS,old[LVDS]);
    wr(d,I915_PP_CONTROL,I915_PANEL_UNLOCK | (old[POWER] & 0xFFFFu));
    if (!wait_bits(d,I915_PP_STATUS,I915_ENABLE | I915_PP_SEQUENCE,I915_ENABLE,5000)) return 0;
    wr(d,I915_PWM_CONTROL,old[PWM]);
    return 1;
}
int i915_display_set_mode(i915_display_t* d, video_framebuffer_t* fb,
                          uint32_t w, uint32_t h) {
    if (!fb || !i915_display_can_modeset(d) || w < 640 || h < 480 ||
        w > SCREEN_MAX_WIDTH || h > SCREEN_MAX_HEIGHT ||
        w > d->panel_width || h > d->panel_height) return 0;
    uint32_t stride = (w * 4u + 63u) & ~63u;
    if (!pin_scanout(d)) return 0;
    uint32_t regs[SNAP_COUNT], old[SNAP_COUNT];
    snapshot_regs(d,regs);
    for (int i = 0; i < SNAP_COUNT; i++) old[i] = rd(d,regs[i]);
    if (!panel_off(d,old) || !pipe_off(d,old)) goto restore_early;
    wr(d,I915_PFIT_CONTROL,0);
    wr(d,preg(d,I915_PIPESRC),((w - 1) << 16) | (h - 1));
    wr(d,I915_PFIT_RATIOS,0);
    wr(d,I915_PFIT_CONTROL,i915_panel_fitting(d->pipe,w,h,d->panel_width,d->panel_height));
    wr(d,preg(d,I915_BCLRPAT),0);
    /* Port i965's fixed normal watermarks; keep CxSR disabled rather than
     * using stale firmware self-refresh values for a different source size. */
    wr(d,I915_FW_BLC_SELF,0);
    wr(d,I915_DSPFW1,(1u << 23) | (8u << 16) | (8u << 8) | 8u);
    wr(d,I915_DSPFW2,(8u << 8) | 8u);
    wr(d,I915_DSPFW3,16u << 24);
    wr(d,I915_LVDS,old[LVDS]);
    wr(d,preg(d,I915_PIPECONF),old[PIPE]);
    if (!wait_bits(d,preg(d,I915_PIPECONF),I915_PIPE_STATE,I915_PIPE_STATE,100)) goto rollback;
    /* Port i9xx_plane_update_noarm/arm: stride/offsets, control, then surface. */
    wr(d,plane_reg(d,I915_DSPSTRIDE),stride);
    wr(d,plane_reg(d,I915_DSPLINOFF),0); wr(d,plane_reg(d,I915_DSPTILEOFF),0);
    wr(d,plane_reg(d,I915_DSPCNTR),old[CONTROL]);
    wr(d,plane_reg(d,I915_DSPSURF),d->gtt_offset);
    if (!panel_on(d,old)) goto rollback;
    *fb = (video_framebuffer_t){d->aperture + d->gtt_offset,32,stride,"Nixel i915 / GMA X3100"};
    return 1;
rollback:
    /* Never rewrite fitter/source registers while the pipe is active. Keep
     * pinned backing alive even on failure because hardware may still fetch. */
    if (!panel_off(d,old) || !pipe_off(d,old)) { d->fault = 1; goto restore_early; }
    for (int i = SOURCE; i <= BORDER; i++) wr(d,regs[i],old[i]);
    for (int i = SR; i < SNAP_COUNT; i++) wr(d,regs[i],old[i]);
restore_early:
    wr(d,I915_LVDS,old[LVDS]);
    wr(d,preg(d,I915_PIPECONF),old[PIPE]);
    (void)wait_bits(d,preg(d,I915_PIPECONF),I915_PIPE_STATE,I915_PIPE_STATE,100);
    wr(d,regs[STRIDE],old[STRIDE]); wr(d,regs[LINEAR],old[LINEAR]); wr(d,regs[TILE],old[TILE]);
    wr(d,regs[CONTROL],old[CONTROL]); wr(d,regs[SURFACE],old[SURFACE]);
    if (!panel_on(d,old)) d->fault = 1;
    return 0;
}
