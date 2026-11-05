# Native Intel iwlegacy port

This directory adapts the supplied Linux iwlegacy reference in
`documentation/linux-for-ref/iwlegacy/` for Intel PRO/Wireless 3945BG/ABG,
PCI `8086:4222` and `8086:4227`. It has a native passive-scan path. A connected
station data path is not yet implemented; Intel 4965 remains unported.

## Native driver path

`iwlegacy.c` independently discovers a 3945 PCI function, checks BAR/ownership,
and reads the EEPROM MAC, revisions, subsystem capabilities and regulatory
channels. Discovery does not enable bus mastering or transmit a frame. Existing
active owners are left alone. Inspection can bring an idle card to D0; afterward
it is stopped with its original PCI command restored, and remains in D0.

`3945.c` ports PLL/APM and EEPROM access with bounded polling and cleanup.
`firmware.c` validates complete API-1/API-2 little-endian firmware containers,
image bounds and alignment. `bsm.c` uploads/verifies bootstrap SRAM. `session.c`
configures the scheduler and five TFD rings, waits for initialization ALIVE,
verifies instruction SRAM, programs runtime pointers with the load bit last,
then waits for and verifies runtime ALIVE. RF-kill and hardware/firmware errors
terminate bounded waits.

`rx.c` owns a coherent 256-entry receive ring with 254 posted 4 KiB buffers,
reserved ring slots, eight-entry producer alignment and bounded recycling.
`command.c` submits actual queue-4 firmware commands, matches command/sequence
completions and preserves buffer ownership across failures. Timed-out commands
poison the queue until hardware reset, rather than reusing a still-owned buffer.

`device.c` loads `/lib/firmware/iwlwifi-3945-2.ucode`, with an API-1 filename
fallback when that file cannot be read. Firmware is not bundled. It prepares
coherent physical firmware/ring storage, starts runtime firmware, then runs the
requested operation. The scan path configures unassociated RXON and sends
passive C_SCAN commands separately for each permitted band. It does not send
probes or promote a passive channel to active transmission.

`scan.c` bounds-checks RX metadata, FCS/error flags and beacon/probe-response
IEs; validates BSSIDs; retains up to 32 infrastructure networks; and recognizes
open, WPA2-CCMP-PSK and unsupported protected configurations. Scan completion
and command rejection/timeouts are handled independently. Failed scans are not
published as successful partial results. Settings displays paged results with
binary SSIDs escaped for display; the binary identities remain in the cache.

The operation stops RX/TX and resets the NIC before releasing allocations. If
DMA idle or bus-master shutdown cannot be confirmed, posted allocations remain
quarantined until reboot. Long firmware operations use a private device mutex;
the HAL publishes snapshots under the shared kernel mutex, allowing the window
manager to run during the scan. The NIC is not kept connected after a scan.

## Additional station components

`power.c` ports EEPROM gain interpolation, the upstream analog/DSP gain tables,
rate clipping, and raw-temperature compensation for C_TX_PWR_TBL. It validates
channel permissions and calibration data before exposing a power command. The
live scan is receive-only; this helper still needs to be scheduled before data
transmission and after RXON/temperature changes.

`../../ieee80211.c` implements bounded infrastructure Ethernet/SNAP conversion.
`../../security/` implements WPA2 four-way/GTK handshakes and CCMP framing/replay
checks over the existing wolfCrypt sources. These components have tests but are
not wired into a persistent association/transmit loop.

## Still missing for connections

Native data/management TX submission and completion, AP station/rate table
configuration, authentication/association/deauthentication state, persistent
DMA ownership and shutdown/recovery, periodic thermal updates, integration of
the supplicant with received/sent EAPOL frames, and selection of the Wi-Fi
interface in the Ethernet/IPv4/DHCP HAL remain unfinished. Settings can scan,
but cannot connect to an AP or send encrypted network traffic yet.

## Validation and provenance

Run `make test-iwlegacy`, `make test-wifi-crypto` and
`make src/kernel/kernel.bin`. Register models exercise PCI ownership, EEPROM,
firmware boot, DMA/ring wrapping, command completion matching/poisoning,
scan parsing, allocation rollback and quarantine. Power/framing and independent
cryptographic tests cover the additional protocol components. Physical-card
interoperability has not been tested.

The firmware/register/power algorithms adapt Linux `3945.c`, `3945-mac.c`,
`common.c`, `common.h`, `commands.h` and `3945.h`, copyright (c) 2003–2011 Intel
Corporation. Native adaptations retain GPL-2.0-only licensing; see `COPYING`.
The copied `csr.h`/`prph.h` retain their complete dual BSD/GPLv2 notices.
