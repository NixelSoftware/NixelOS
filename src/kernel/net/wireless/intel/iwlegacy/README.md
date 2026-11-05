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
  This component is invoked by the explicit firmware diagnostic, not discovery.
* HAL and shell integration: Settings uses vendor-neutral status text; `netdev`
  shows the 3945 variant, subsystem ID, initialization status and EEPROM/MAC,
  channel-count and radio-switch information when available.

`sys_wifi_probe()` inspects the first matching 3945 PCI function, independently
of Ethernet and b43 discovery. Devices already bus mastering or initialized are
left alone. An idle device may be brought to D0 for inspection; afterward the NIC
is stopped and the original PCI command is restored. It remains in D0 rather
than restoring a previous PCI power state. PCI inventory and BAR mapping use the
kernel's existing firmware-assigned, 32-bit MMIO services.

## Firmware startup and transport components

`rx.c` manages a coherent 256-entry receive ring with 254 posted 4 KiB buffers,
the two-slot hardware gap, eight-entry producer alignment, bounded consumer
processing, notification length validation and buffer recycling. The callback
borrows a packet only until it returns. Completion indices outside the posted
window are rejected; callers retain allocations if DMA stop is not acknowledged.

`session.c` configures the scheduler and five 256-entry TFD queues, starts
bootstrap firmware, waits for initialization ALIVE, verifies instruction SRAM,
programs runtime DMA pointers with the load bit last, and waits for/validates
runtime ALIVE. Both waits are bounded and check RF-kill and firmware/hardware
errors. This configures empty TX queues; command submission/completions and data
transmission are not implemented.

On the first Intel Settings scan attempt, `device.c` checks
`/lib/firmware/iwlwifi-3945-2.ucode`, falling back to
`/lib/firmware/iwlwifi-3945-1.ucode` if the API-2 file cannot be read. Firmware
is not redistributed here. Invalid images fail validation before NIC startup.
The driver allocates coherent physical memory using the kernel paging allocator,
copies/pins the firmware images, brings the NIC through runtime ALIVE, then
stops RX/TX and resets the device. PCI bus mastering is only enabled after the
queues are configured. It is disabled again on cleanup.

This is a firmware diagnostic stage, not a scan: no RXON/scan/association or
frame-transmission command is issued. `netdev` reports the verified firmware
version or a startup error, while Settings reports that scanning is unavailable.
A successful result is cached until a new adapter probe; the NIC is stopped,
not kept connected. Failed DMA-idle/master-stop acknowledgements quarantine
posted allocations until reboot, rather than allowing DMA into freed RAM.
Allocation and pre-activation failures release all allocated pages.

Still required: command submission and completion dispatch, calibration and
temperature-based transmit power, actual scanning, authentication/association,
encryption/key installation and 802.11/Ethernet bridging. The shared
`../../security` module supplies wolfCrypt-based WPA2 PMK,
PTK and EAPOL MIC primitives, but these are not a four-way handshake. Intel 4965
is still unported. Host tests do not establish physical-card compatibility.

## Validation

Run `make test-iwlegacy`. The host register models and lifecycle tests exercise firmware parsing,
regulatory data, APM/EEPROM sequencing, RF-kill polarity, indirect register
routing, bootstrap upload/verification, bounded timeouts, cleanup, PCI matching
and refusal to touch an existing owner, RX wrap/recycling, ALIVE ordering,
firmware-file failures, allocation rollback and DMA quarantine. These tests do not transmit RF and do
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
