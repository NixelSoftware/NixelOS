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
/* Render command streamer (gen4 RCS), from i915_reg.h / intel_ring_buffer. */
#define I915_RING_TAIL       0x2030u
#define I915_RING_HEAD       0x2034u
#define I915_RING_START      0x2038u
#define I915_RING_CTL        0x203Cu
#define I915_HWS_PGA         0x2080u
#define I915_RING_VALID      1u
#define I915_RING_SIZE_MASK  0x001FF000u   /* (bytes - 4096) */
#define I915_RING_ADDR_MASK  0x001FFFFCu
/* MI commands (MI_INSTR(op, flags) = op << 23 | flags). */
#define MI_NOOP              0u
#define MI_FLUSH             (0x04u << 23)
#define MI_EXE_FLUSH         (1u << 1)
#define MI_BATCH_BUFFER_END  (0x0Au << 23)
#define MI_STORE_DWORD_INDEX (0x21u << 23 | 1u)
#define MI_BATCH_BUFFER_START (0x31u << 23)
#define MI_BATCH_GTT         (2u << 6)
/* Error/status registers, read-only diagnostics (i915_reg.h, gen4 variants). */
#define I915_PGTBL_CTL       0x2020u
#define I915_PGTBL_ER        0x2024u
#define I915_IPEIR           0x2064u
#define I915_IPEHR           0x2068u
#define I915_ACTHD           0x2074u
#define I915_EIR             0x20B0u
#define I915_ESR             0x20B8u
/* Status page dwords 0..0x1F are written by hardware (i915 starts its software
 * area at I915_GEM_HWS_INDEX = dword 0x20). Byte offsets used as MI_STORE_DWORD_INDEX
 * operands. */
#define I915_HWS_SEQNO       0x80u
#define I915_HWS_SCRATCH     0x84u
#define I915_MI_MODE         0x209Cu
#define I915_STOP_RING       (1u << 8)
#define I915_MODE_IDLE       (1u << 9)
#define I915_INSTPM          0x20C0u
#define I915_MASKED_ENABLE(b)  (((b) << 16) | (b))   /* gen4+ masked-bit registers */
#define I915_MASKED_DISABLE(b) ((b) << 16)
#endif
