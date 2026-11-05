/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef NIXEL_B43_H
#define NIXEL_B43_H
#include "../../../../drivers/pci.h"

typedef enum {
    B43_ABSENT, B43_BUS_UNSUPPORTED, B43_PROBE_FAILED,
    B43_LEGACY, B43_CORE_FOUND
} b43_probe_state_t;
typedef struct {
    b43_probe_state_t state;
    uint16_t vendor, device, chip;
    uint8_t core_revision, core_index;
} b43_info_t;
/* Discovery only: does not reset the core, start DMA or transmit RF. */
void b43_probe(b43_info_t* info);
const char* b43_probe_description(b43_probe_state_t state);

/* b43/main.c firmware file format, native endian-independent parser.
 * Validate the entire file before exposing any values to a caller. */
typedef struct {
    const uint8_t* data;
    uint32_t length, count;
    uint8_t type;
} b43_firmware_t;
int b43_firmware_parse(const uint8_t* bytes, uint32_t length,
                       uint8_t expected_type, b43_firmware_t* out);
typedef int (*b43_iv_writer_t)(void* context, uint16_t offset,
                              uint32_t value, uint8_t width);
int b43_firmware_initvals(const b43_firmware_t* fw,
                          b43_iv_writer_t write, void* context);
#endif
