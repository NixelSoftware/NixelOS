# Native Intel iwlegacy port

This directory adapts the supplied Linux iwlegacy reference in
`documentation/linux-for-ref/iwlegacy/` for Intel PRO/Wireless 3945BG/ABG,
PCI `8086:4222` and `8086:4227`. It implements passive scanning and a persistent infrastructure station path
for open and WPA2-CCMP-PSK networks. Intel 4965 remains unported.

## Native driver path

`iwlegacy.c` independently discovers a 3945 PCI function, checks BAR/ownership,
and reads the EEPROM MAC, revisions, subsystem capabilities and regulatory
channels. Discovery does not enable bus mastering or transmit a frame. Existing
native driver owners are left alone. PCI bus-master enable and INIT_DONE alone
are treated as inherited hardware state, not ownership: PCI DMA is disabled
before BAR probing and that state is reset with bounded waits. Before any host
DMA is posted, a missing stop-master acknowledgement is tolerated after reset
clears INIT_DONE, matching the upstream startup behavior. Live-DMA shutdown
still requires acknowledgement or quarantines the pinned buffers. Inspection can bring the card to D0; afterward
it is stopped with its original PCI command restored, and remains in D0.

`3945.c` ports PLL/APM and EEPROM access with bounded polling and cleanup.
`firmware.c` validates complete API-1/API-2 little-endian firmware containers,
image bounds and alignment. `bsm.c` uploads/verifies bootstrap SRAM. `session.c`
configures the scheduler and five TFD rings, waits for initialization ALIVE,
verifies instruction SRAM, programs runtime pointers with the load bit last,
then waits for and verifies runtime ALIVE. Before commands are submitted,
it checks the internal RF-kill state and waits up to one second for a nonzero,
valid thermal reading, matching the upstream post-ALIVE startup ordering. RF-kill and hardware/firmware errors
terminate bounded waits.

`rx.c` owns a coherent 256-entry receive ring with 254 posted 4 KiB buffers,
reserved ring slots, eight-entry producer alignment and bounded recycling.
`command.c` submits actual queue-4 firmware commands, matches command/sequence
completions and preserves buffer ownership across failures. Timed-out commands
poison the queue until hardware reset, rather than reusing a still-owned buffer.
The MAC wake request is held through command completion, including delayed DMA
fetches, and the doorbell is flushed with a PCI read before polling. Every
completion/error path releases the wake request. Command timeout diagnostics
capture GP1 (sleep/block state), queue-4 configuration and global TX status
before shutdown changes those registers.

`device.c` loads `/lib/firmware/iwlwifi-3945-2.ucode`, with an API-1 filename
fallback when that file cannot be read. Firmware is not bundled. It prepares
coherent physical firmware/ring storage, starts runtime firmware, then runs the
requested operation. The scan path sends awake power/BT setup commands,
configures unassociated RXON, programs calibrated TX power, restores the
broadcast station and programs both rate fallback tables. It then sends passive
C_SCAN commands separately for each permitted band. It does not send probes
or promote a passive channel to active transmission.

`scan.c` bounds-checks RX metadata, FCS/error flags and beacon/probe-response
IEs; validates BSSIDs; retains up to 32 infrastructure networks; and recognizes
open, WPA2-CCMP-PSK and unsupported protected configurations. Scan completion
and command rejection/timeouts are handled independently. Failed scans are not
published as successful partial results. Settings displays paged results with
binary SSIDs escaped for display; the binary identities remain in the cache.

Scan/firmware-check operations stop RX/TX and reset the NIC before releasing allocations. If
global DMA stop/reset or PCI bus-master shutdown cannot be confirmed, posted allocations remain
quarantined until reboot. Long firmware operations use a private device mutex;
the HAL publishes snapshots under the shared kernel mutex, allowing the window
manager to run during the scan. Connections retain the firmware, DMA rings and
command/frame storage until disconnect or failure. Before enabling PCI DMA,
it clears the 3945 RETRY_TIMEOUT configuration byte (0x41), matching Linux’s
workaround for PCI TX retries interacting with CPU C3 state. Scans require disconnecting
first, so an active data interface is never reset by discovery.

## Infrastructure station path

`tx.c` submits management/data C_TX on queue 0 using separate, aligned command
and MPDU-payload transfer buffers. It matches completions by sequence, preserves
software data sequence numbers, wraps the TFD ring, and never recycles an
unacknowledged DMA allocation after timeout.

`station.c` configures awake power mode, legacy EDCA, RXON, AP/broadcast stations,
control/data rate fallback tables and EEPROM-calibrated TX power. It performs
open-system authentication and association with bounded retries and programs
beacon timing/AID. Scan results preserve supported rates, TSF and the complete
AP RSN IE for negotiation. The lowest supported legacy rate is used; adaptive
rate selection is not implemented.

Ethernet frames use the shared three-address SNAP bridge. WPA2 connections run
the four-way and GTK rekey exchanges over actual EAPOL TX/RX, with software CCMP,
authorized-port gating and replay protection. Duplicate authenticated handshake
messages preserve installed packet counters. The port opens after M4 completes.
Borrowed DMA frames are copied into a bounded deferred queue before invoking
management, crypto or network callbacks, avoiding recursive RX-ring processing.

Settings allows selecting an AP, entering a masked password, connecting and
disconnecting. Passwords are erased after the attempt and are not saved. A
successful connection selects driver 5 in the network HAL, replaces the old
wired interface/stack identity, and requests DHCP. RX, MAC/IP operations and
diagnostics use that interface. Disconnect sends best-effort deauthentication;
RF-kill, firmware errors, handshake/receive/TX timeouts close the port, clear
keys/addressing, stop DMA and release or quarantine its allocations. Raw
temperature is checked every minute and power recomputed for changes of 6 units.

For a `-9` (`IL_DMA_STOP`) failure, reboot before retrying: pinned buffers are
quarantined until reboot. `wifid-status` preserves the original operation error,
failed shutdown checks (RX, TX, global/reset, PCI-master), and init/runtime ALIVE
progress. It also shows the boot stage (1 BSM, 2 init ALIVE, 3 init verification,
4 runtime ALIVE, 5 runtime verification, 6 radio/thermal calibration, 7 ready)
and the last command with pending/poisoned flags. A pending command identifies
an acknowledgement timeout; command 0x80 with pending=0 and error -3 identifies
a scan-completion timeout. A confirmed global DMA stop supersedes per-queue idle timeouts; the
PCI master bit must also be confirmed clear before allocations are freed.

## Supported limits

This is an infrastructure 3945 station implementation, not feature parity with
Linux mac80211. Only EEPROM-valid active non-DFS channels are used for joining.
Passive/DFS scanning remains available, but joining such channels is rejected.
WEP, WPA1/TKIP, enterprise EAP, WPA3/SAE, required PMF, hidden-SSID entry, ad-hoc/AP
mode, roaming, suspend/resume, QoS negotiation and adaptive rate selection are
not implemented. TX is single-inflight and polled; contention returns busy.
A failed connection can be retried after a new scan; there is no automatic roam.

The HAL obtains WPA2 nonces from CPUID-checked RDRAND when available, with
upstream Jitterentropy 3.6.3 as a fallback on older CPUs with a TSC. The fallback
collects CPU execution timing variation, conditions it with SHA3-256, and keeps
startup and runtime health tests enabled. It never seeds a PRNG from a clock,
MAC address or password. Health/allocation failures erase the output and refuse
the connection. Entropy startup runs outside the shared UI/network mutex.
Open connections do not need entropy. Firmware is still an external prerequisite.
See `src/kernel/crypto/README.md` for the port and validation details.

## Validation and provenance

Run `make test-iwlegacy`, `make test-wifi-crypto` and
`make src/kernel/kernel.bin`. Register models exercise PCI ownership, EEPROM,
firmware boot, DMA/ring wrapping, command completion matching/poisoning,
scan parsing, allocation rollback and quarantine. The station model additionally
exercises association, 4,300 data frames across sequence/ring wrap, RX delivery,
regulatory rejection, RF-kill and retained TX timeout ownership. An independent
Python-generated AP transcript drives the native station through WPA2 M1–M4
and encrypted data TX. Power/framing and independent
cryptographic tests cover the additional protocol components. Physical-card
interoperability has not been tested.

The firmware/register/power algorithms adapt Linux `3945.c`, `3945-mac.c`,
`common.c`, `common.h`, `commands.h` and `3945.h`, copyright (c) 2003–2011 Intel
Corporation. Native adaptations retain GPL-2.0-only licensing; see `COPYING`.
The copied `csr.h`/`prph.h` retain their complete dual BSD/GPLv2 notices.

## Shell diagnostics

`netdev` lists PCI hardware identities and matching drivers. Run `wifid-status`
for the cached radio/firmware state, original errors, ALIVE progress, last
command and command-transport register snapshots. This read-only command does
not reset the device or start a scan. For a first-command 0x77 timeout, include
Command GP1, TX queue 4 config and TX hardware status when reporting the failure.

## First-command timeout handling

The PCI register BAR is mapped with PCD/PWT (uncached) before the first MMIO
access, including EEPROM discovery. Only the BAR's 4 KiB page changes memory
type; neighboring mappings retain their flags. Existing and new app address
spaces share the kernel MMIO table. RAM and private app mappings are rejected.
`make test-mmio-paging` exercises this mapping at ring 0 in QEMU.

Polled boot, command, scan and station loops acknowledge both CSR_INT and
CSR_FH_INT_STATUS, matching the Linux interrupt tasklet. Startup repeats the
software RF-kill clear handshake before BSM. The initial 0x77 power table keeps
firmware sleep disabled and derives PCI_PM from PCIe Link Control, as Linux
does, for both scans and connections.

On command failure, wifid-status additionally preserves GP control, flow-handler
interrupt status, the TX shared-table base, scheduler mode/active FIFOs, and RX
read/closed positions before shutdown. Connection failures now retain the same
command diagnostics as scan failures. These changes address startup/transport
differences; resolving a specific physical-card timeout still requires a boot
and scan on that hardware. Queue poisoning and DMA quarantine remain enforced.

A scan command always uses SEQ_HUGE_FRAME, including a short channel list.
The passive command includes the same basic-rate TX metadata, wildcard probe
frame template and per-channel radio defaults used by Linux's passive path.
The channel list follows the 26-byte template, rather than the fixed header.
Every channel type remains zero and good_CRC_th remains 0xffff, so the template
is not transmitted and reception cannot promote the scan to active probing.
Tests validate these ABI fields, small-scan huge sequencing, setup ordering,
and calibration/setup failures before scanning.

C_ADD_STA replies are validated by their first status byte (exactly 1 means
success), accepting trailing RX padding. An empty or rejected reply still
fails. Both scan and connection setup use this rule. For a completed command
that fails validation, wifid-status shows reply ID/flags, payload length and the
first four payload bytes as a little-endian status word. This distinguishes
reply framing from a firmware rejection without disabling error handling.

Scan completion accepts the packed 11-byte notification and trailing RX padding.
Truncated notifications and incomplete channel counts still fail. On a failure
following an accepted C_SCAN, wifid-status includes completion length, completed
and expected channel counts, and firmware completion status. The register model
uses a 12-byte padded notification; separate parser tests cover truncation,
11-byte packed notifications and incomplete scans.

An unassociated RXON clears the firmware station table; the transition to an
associated RXON preserves it. The station path adds broadcast/AP entries only
after the unassociated RXON, avoiding duplicate C_ADD_STA requests during
association. Tests model this lifecycle and return status 0x02 for duplicate
entries. Genuine C_ADD_STA rejection still fails the connection and preserves
the firmware reply in diagnostics.

Data TX holds the MAC wake request through delayed DMA completion, flushes the
doorbell and restores access after RX-ring updates. Completion and teardown
release the request. The station model exercises a delayed data completion and
retained wake ownership. TX status and AP deauthentication reason
codes are retained in netdiag; CCMP receive failures increment RX errors.

IPv4 queues packets when a resolved destination encounters a busy NIC, as well
as while awaiting ARP. This preserves DNS/ICMP/UDP traffic on the single-inflight
Wi-Fi transmitter. DHCP requests DNS/gateway settings in option 55; DNS retries
lost queries with the same socket and transaction ID, within its existing timeout.
`make test-wifi-network` exercises the real IPv4/UDP/DNS path with a busy-slot
peer, lost queries, queue bounds/copy/expiry, ARP, and DHCP option negotiation.

Non-QoS TX requests firmware sequence control, matching Intel's Linux driver;
QoS TX retains software sequence control. Tests verify a firmware sequence
rewrite does not invalidate software CCMP. Station entries enable both antenna
bits in rate_n_flags as upstream does. After a live disconnect wifid-status
preserves runtime IRQ/FH status and data-queue pending/poisoned flags, separately
from the last connection-setup command reply (often 0x47). Regression cases
separate firmware SW_ERR from malformed TX completions before reset.
