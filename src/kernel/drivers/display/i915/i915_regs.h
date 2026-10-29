/* SPDX-License-Identifier: MIT
 * Copyright © 2023-2025 Intel Corporation
 * Selected gen4 definitions adapted from supplied i915 display register headers.
 * See COPYING and PORT.md. Offsets are byte addresses, not Linux _MMIO types.
 */
#ifndef NIXEL_I915_REGS_H
#define NIXEL_I915_REGS_H
#define I915_ENABLE          (1u << 31)
#define I915_PIPE_STATE      (1u << 30)
#define I915_STRIDE          0x1000u
#define I915_DSPCNTR         0x70180u
#define I915_DSPLINOFF       0x70184u
#define I915_DSPSTRIDE       0x70188u
#define I915_DSPSURF         0x7019Cu
#define I915_DSPTILEOFF      0x701A4u
#define I915_PIPECONF        0x70008u
#define I915_PIPEDSL         0x70000u
#define I915_PIPESRC         0x6001Cu
#define I915_HTOTAL          0x60000u
#define I915_VTOTAL          0x6000Cu
#define I915_BCLRPAT         0x60020u
#define I915_LVDS            0x61180u
#define I915_LVDS_PIPE_B     (1u << 30)
#define I915_PFIT_CONTROL    0x61230u
#define I915_PFIT_RATIOS     0x61234u
#define I915_PFIT_PILLAR     (2u << 26)
#define I915_PFIT_LETTER     (3u << 26)
#define I915_PP_STATUS       0x61200u
#define I915_PP_CONTROL      0x61204u
#define I915_PANEL_UNLOCK    0xABCD0000u
#define I915_PANEL_ON        1u
#define I915_PP_SEQUENCE     (3u << 28)
#define I915_PWM_CONTROL     0x61250u
#define I915_FW_BLC_SELF     0x20E0u
#define I915_DSPFW1          0x70034u
#define I915_DSPFW2          0x70038u
#define I915_DSPFW3          0x7003Cu
#define I915_GTT             0x80000u
#define I915_GTT_ENTRIES     (0x08000000u / 4096u)
#define I915_FBC_CONTROL     0x3208u
#define I915_CURSOR_CONTROL  0x70080u
#define I915_ADPA            0x61100u
#define I915_SDVOB           0x61140u
#define I915_SDVOC           0x61160u
#endif
