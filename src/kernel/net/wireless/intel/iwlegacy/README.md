# Native Intel iwlegacy port

This directory adapts Intel's Linux iwlegacy implementation supplied in
`documentation/linux-for-ref/iwlegacy/`. It targets the PRO/Wireless 3945BG/ABG
first (PCI `8086:4222` and `8086:4227`). This is an initial hardware/firmware
bring-up port, not a working station driver. The Linux 4965 path is not ported.

## Implemented

* Native PCI discovery, including BG-only subsystem variants from the upstream
  match table, firmware-assigned BAR validation and exclusive-access checks.
* 3945 APM/PLL initialization, bounded clock/MAC-access waits, indirect peripheral
  access, DMA/BSM clocks, internal interrupt masking and radio-chip reset.
* The 1024-byte EEPROM signature, ownership and read sequence. Decode the factory
  MAC, EEPROM/board revisions and all 56 regulatory channel entries. Retain
  EEPROM flags, DFS/passive restrictions and signed transmit-power limits; omit
  5 GHz permissions on BG-only cards.
* Stop-master/software-reset/initialization cleanup after inspection. The probe
  never enables PCI bus mastering or transmits a frame; the APM clock enables do
  not start DMA without queue configuration.
* Strict little-endian firmware parsing for API versions 1 and 2, including
  exact file lengths, overflow-safe bounds and aligned image sizes. The native
  boot path requires all five images and limits bootstrap code to the physical
  1 KiB BSM SRAM, rather than the larger instruction-SRAM limit in the Linux
  parser. Firmware buffers are caller-owned and must remain immutable.
* BSM image upload/readback, initial-image DMA address/size programming, bounded
  bootstrap copy completion and enabling subsequent power-management reloads.
  This component is tested but not invoked by the live probe.
* HAL and shell integration: Settings uses vendor-neutral status text; `netdev`
  shows the 3945 variant, subsystem ID, initialization status and EEPROM/MAC,
  channel-count and radio-switch information when available.

`sys_wifi_probe()` inspects the first matching 3945 PCI function, independently
of Ethernet and b43 discovery. Devices already bus mastering or initialized are
left alone. An idle device may be brought to D0 for inspection; afterward the NIC
is stopped and the original PCI command is restored. It remains in D0 rather
than restoring a previous PCI power state. PCI inventory and BAR mapping use the
kernel's existing firmware-assigned, 32-bit MMIO services.

## Remaining integration

There is no file loader for `iwlwifi-3945-2.ucode` (fallback
`iwlwifi-3945-1.ucode`) yet; no firmware is redistributed here. `il3945_load_bsm()`
requires pinned coherent initial-code/data buffers with 32-bit DMA addresses,
configured RX/TX queues and an exclusively owned, clocked NIC. It copies the
bootstrap but does not release the embedded processor or handle ALIVE events.
After an error the caller must reset/stop DMA before freeing firmware buffers.

RX/TX descriptor rings, command queue and completion dispatch, initialization
ALIVE to runtime-firmware transition, calibration and temperature-based transmit
power, scanning, authentication/association, encryption and 802.11/Ethernet
bridging still need to be ported. EEPROM inspection does not establish physical
card compatibility or that a network can be joined. Settings continues to report
that scanning is unavailable.

## Validation

Run `make test-iwlegacy`. The host register model exercises firmware parsing,
regulatory data, APM/EEPROM sequencing, RF-kill polarity, indirect register
routing, bootstrap upload/verification, bounded timeouts, cleanup, PCI matching
and refusal to touch an existing owner. These tests do not transmit RF and do
not emulate a full Intel radio. Build `make src/kernel/kernel.bin` for the native
32-bit freestanding integration check.

## Provenance and license

`3945.c`, `bsm.c`, `firmware.c` and `eeprom.c` adapt the Linux iwlegacy
`3945.c`, `3945-mac.c`, `common.c`, `common.h` and `3945.h` algorithms and layouts.
Copyright (c) 2003–2011 Intel Corporation. All rights reserved. Native adaptations
retain GPL-2.0-only licensing; see `COPYING`. The original `csr.h` and `prph.h`
register definitions are copied unchanged with their complete Intel copyright
and dual BSD/GPLv2 notices. Linux device model, mac80211, workqueue and allocator
interfaces are not copied into the native kernel.
