# Intel 3945 runtime firmware crash analysis

Analysis date: 2026-11-08.

## Main finding

The reported NMI PC, **0x3564**, points to a hardware-status polling loop in the
actual runtime firmware. The reported assertion PC, **0x8b6**, belongs to the
shared error-reporting machinery. The strongest current explanation is that
the firmware gets stuck waiting for internal hardware state, then takes a
fatal interrupt. A TX/radio state-machine stall is a stronger lead than DNS,
DHCP, WPA2 entropy, or a missing host keepalive.

The exact meaning of the hardware status bit is not established. There are no
firmware symbols or vendor definitions for the extension register in this
repository. Calling it a particular DMA, RF, or encryption bit would be speculation.

## Binary identity and extraction

The repository file `firmware/iwlwifi-3945-2.ucode` is 150100 bytes, version
15.32.2.9, SHA-256:

```
9bccc66fab027d18f0977fa737816d3e67938d4a9f5cf0790df8e45e19168f20
```

A separately downloaded copy from the
[upstream linux-firmware repository](https://gitlab.com/kernel-firmware/linux-firmware/-/raw/main/intel/iwlwifi/iwlwifi-3945-2.ucode)
has the identical SHA-256. This verifies the repository file; it does not hash
the file on the user's boot medium or the card's live SRAM.

The container is six little-endian 32-bit words, followed by five raw sections:

| Section | File offset | Bytes |
| --- | ---: | ---: |
| Runtime instructions | 0x18 | 80968 |
| Runtime data | 0x13c60 | 32768 |
| Initialization instructions | 0x1bc60 | 2672 |
| Initialization data | 0x1c6d0 | 32768 |
| Bootstrap | 0x246d0 | 900 |

The runtime instruction image was extracted and decoded using GNU binutils
2.42, ARC700/ARCompact mode. Its reset vectors, calls, returns, stack operations,
and the precise reported crash addresses form coherent control flow. Some
vendor extension instructions/registers remain unidentified; this is not a
complete decompilation or an identification of the exact ARC core model.

## What the crash addresses actually show

### 0x8b6 is an error-reporting address

Relevant decoded runtime instructions:

```asm
0x028c: mov_s r1,0x74
0x028e: mov_s r3,0x5
0x0290: bl.d  0x08ac
0x0294: mov_s r4,r1          ; branch delay slot

0x08ac: push_s blink
0x08ae: bl.d  0x06f4        ; save registers
0x08b2: bmsk  r2,r0,0x17    ; branch delay slot
0x08b6: mov_s r13,r1
0x08b8: mov_s r14,r3
0x08ba: mov   r16,r4
0x08be: bmsk  r3,ilink,0x17
0x08c2: bmsk  r15,r30,0x17
0x08c8: bmsk  r1,blink,0x17
...
0x08d8: bl_s  0x0824        ; append error record
```

The call at 0x8ae leaves its return address 0x8b6 in `blink`. The logger copies
that register into the error record. Consequently, 0x8b6 is not sufficient to
identify a malformed command or the original failing function.

Likewise, the record's final word, displayed as `Line` by Linux and `line` by
the native shell, comes from the literal **0x74 = 116** in this path. It cannot
be used to locate line 116 of a firmware source file. Other assertion paths
may use that field differently.

The recorded blink2 0x274 also fits this fatal interrupt path. The ilink1 value
0x320 points to the firmware's idle sleep loop (0x31c: `sleep 0`, 0x320: branch
back to 0x31c). That is internal firmware idle state, not proof the host failed
to keep the Wi-Fi connection alive.

### 0x3564 is the interrupted hardware wait

```asm
0x3514: push_s blink
...
0x351e: cmp_s r0,1
0x3522: bne_s 0x3560
        ; optional writes to internal peripheral addresses
...
0x3560: btst  r32,10
0x3564: bne_s 0x3560
0x3566: ld_s  r0,[gp,192]
```

If the recorded NMI PC is 0x3564, execution was at the branch of the loop that
waits while bit 10 is set. This routine has several callers, including paths
that examine per-operation status, request internal state changes, and then
wait for hardware. Its callers include 0x3e90, 0x3f0e, 0x4658, 0x4926 and
0x4a38. Without the interrupted stack or preceding event history, the exact
caller is unknown.

This supports a hardware-state stall interrupted by the fatal path. A watchdog
is a plausible mechanism, but the available evidence does not prove which
interrupt source fired. The binary also writes 200000 (0x30d40) to an internal
peripheral location; without the peripheral definition, that alone does not
prove a 200 ms watchdog timeout.

## Interpretation of the captured command

| Captured field | Meaning and limitation |
| --- | --- |
| Sequence 0x0007 | Queue 0, descriptor index 7. This is the last submitted TX, not a proven offending-command sequence. |
| Frame length 76 | 24 bytes MAC header plus 52 bytes payload. |
| Flags 0x2008 | ACK requested and firmware sequence control. Matches Linux's non-QoS path. |
| Rate 0x0a | 1 Mbps CCK. |
| Station 0 | Native AP station. |
| Security 0 | Firmware encryption disabled; consistent with the native software-CCMP design. It does not prove an unencrypted over-the-air frame. |
| TFD 0x02000000 | Two transfer buffers, no final padding. |
| First/payload 80/52 | Four-byte command header + 52-byte TX command + 24-byte MAC header; separate 52-byte payload. |

For a normal WPA2 ARP packet, 52 bytes could be 8 bytes SNAP + 28 bytes ARP +
8 bytes CCMP header + 8 bytes CCMP MIC. That is a size-based hypothesis; the
diagnostic snapshot does not retain packet contents.

The lengths and flags agree with `il3945_tx_skb`, `il3945_build_tx_cmd_basic`,
`struct il3945_tx_cmd`, and `il3945_hw_txq_attach_buf_to_tfd` in the supplied
Intel-authored Linux references. This rules out those particular scalar
mismatches. It does not verify the DMA addresses, device-visible bytes,
station-table state, or internal hardware progress.

## Causes to investigate, in priority order

1. **TX/radio hardware-state transition that never completes.** This is the
   closest explanation to the actual interrupted instruction. Possible triggers
   include transmit cancellation, retry/ACK handling, or radio-state transitions.
   The exact operation and meaning of r32 bit 10 are unproven.
2. **A native setup or sequencing difference triggering that firmware path.**
   The native driver fixes all data to the lowest AP rate, redirects every
   fallback-table entry to that rate, uses queue 0 for all frames, installs
   uniform legacy EDCA parameters, and disables Bluetooth arbitration. Linux
   supplies a fuller retry graph and mac80211 queue/configuration lifecycle.
   These are real differences, not individually established bugs. At 1 Mbps,
   the fallback target is already the lowest rate, so changing higher-rate
   fallback entries alone may have no effect on this failure.
3. **DMA ownership, visibility, or queue-state corruption.** The snapshot only
   retains software metadata. Linux normally has distinct DMA mappings and
   storage per in-flight descriptor. At the time of the initial capture, the
   native driver reused a single buffer after a matching TX response. That
   can be valid for single-inflight TX, but
   relies on completion matching and hardware ownership assumptions. The host
   model does not exercise asynchronous device DMA, PCI coherency, or firmware
   prefetch behavior. No concrete premature reuse has been established.
4. **Firmware defect exposed by traffic or the AP.** A historical Linux report
   has the same 15.32.2.9 / 0x8b6 / 0x274 / 0x320 / 116 signature and an N_ERROR
   naming C_TX. The maintainer requested verbose logs and suspected interaction
   with mac80211 changes; that report does not identify a universal fix. The
   same logger signature can cover different interrupted PCs.
5. **Power-management interaction or physical radio/PCI trouble.** These remain
   possible. Holding MAC access and disabling firmware sleep reduces one class
   of sleep race, but does not prove every internal radio state is correct.
   A repeatable comparison with Linux on this exact card would help separate
   native-driver behavior from a hardware/AP/firmware combination.

## What is unlikely to explain this record

- A DNS configuration error cannot directly explain firmware spinning at
  0x3564; DNS traffic can merely trigger a transmit that exposes the failure.
- DHCP success confirms some traffic works, not that sustained unicast TX works.
- Host keepalive expiry follows the native 30-second timeout path, returning
  IL_TIMEOUT (-3), rather than a recorded firmware SW_ERR/SYSASSERT.
- Bad CCMP authentication normally results in AP rejection or absent network
  replies. It is not evidence for this internal firmware wait. Software
  encryption still needs to be isolated experimentally, because different
  frame layouts can expose a firmware issue.
- The repository blob is byte-identical to upstream. Replacing it with another
  copy of the same version is not an evidence-backed remedy.
- Lack of CSR HW_ERR does not completely exclude DMA or hardware problems;
  SW_ERR reports how the failure was surfaced, not every possible cause.

## Most useful next work

Capture the firmware event log immediately before reset, with bounded pointer
and count validation. The current `not captured` means no N_ERROR record was
retained; it does not mean the firmware found no error. A one-pass RX drain
can miss a notification whose completion becomes visible later. The firmware
logger and RX producer may also be unable to finish after this stall.

Retain queue-0 registers, descriptor addresses and non-secret command metadata,
the final TX response status, and a small chronological submission/completion
history. A host `pending=0` is not evidence that the firmware's internal radio
state machine is idle.

Then compare one variable per hardware run: open versus WPA2 on the same band
and AP; native fixed-rate/retry configuration versus the Linux setup; and
ordinary operation under Linux on the same card. These comparisons distinguish
crypto/layout, radio/retry configuration, and hardware/firmware causes better
than changing unrelated network-stack timeouts.

Do not patch out the firmware loop, suppress SW_ERR, or recycle possibly owned
DMA memory to keep the connection apparently alive. Those changes would hide
the failure without restoring hardware progress.

## Reproducing the binary inspection

Use an ARC-capable GNU objcopy/objdump, not an x86 disassembler. Extract exactly
80968 bytes at file offset 24 into `runtime.bin`, then:

```sh
arc-linux-gnu-objcopy -I binary -O elf32-littlearc -B ARC700 \
  --rename-section .data=.text,alloc,load,readonly,code,contents \
  runtime.bin runtime.elf
arc-linux-gnu-objdump -d -M cpu=arc700 runtime.elf
```

Giving the extracted section code attributes is necessary: the raw binary's
default data section can otherwise be printed as `.word` values. Address
interpretation here assumes the runtime instruction mapping at zero, consistent
with the reset vectors and the native instruction-SRAM verification path.

## Sources

- Local Intel Linux references: `3945-mac.c` (TX and error log), `3945.c`
  (TFDs, scheduler, rate setup), `commands.h` (command ABI), under
  `documentation/linux-for-ref/iwlegacy/`.
- [Intel driver maintainer's response to matching Linux crash records](https://lkml.rescloud.iu.edu/hypermail/linux/kernel/1303.1/00143.html).
- Upstream firmware blob linked above; independently downloaded and hashed.

Static disassembly does not establish the missing hardware bit definition or
reproduce the crash.

## Follow-up repair attempt

The native driver now preserves an existing MAC_ACCESS_REQ when replenishing
the RX ring. Previously that helper cleared the wake request owned by an
in-flight command or TX, leaving a gap before its caller restored it. The
awake power configuration may mitigate this gap; it is not established as
the cause of the recorded firmware stall.

Station configuration commands now wait for pending data/management TX to
complete, with a bounded timeout and no reuse of timed-out DMA storage. This
also covers an association response arriving before its TX completion and
thermal power updates while a frame is pending.

SW_ERR handling polls for late N_ERROR DMA notifications for up to roughly
one millisecond, dispatching only error notifications after the fault. This
improves evidence retention; it does not recover stalled firmware. Firmware
event-log capture remains outstanding.

Regression coverage exercises RX wake ownership, delayed association TX
completion, and delayed error notifications during runtime and command waits.
Physical-card verification is still required before calling the crash fixed.

## Second repair attempt: distinct TX DMA storage per descriptor

Queue 0 now has a separate 4 KiB command/payload slot for each of its 256 TFDs.
The original design pointed all descriptors at one mutable 4 KiB buffer;
submitting the next frame overwrote the bytes referenced by every earlier
descriptor. That was intended to be safe after a matching completion, but
made correctness depend on the device never referring to those bytes again.
Distinct slots remove that cross-descriptor aliasing and are closer to the
Linux per-entry command and mapped-payload layout. A slot is reused only
after the producer wraps, with matching completions still required.

This changes TX storage from 4 KiB to 1 MiB per connection. The entire block
stays pinned until acknowledged DMA shutdown, or is quarantined on failure.
Allocation failure aborts connection cleanly. Single-inflight operation and
software CCMP remain in use; retry tables and the selected rate are unchanged.
This is an experimental isolation of DMA lifetime assumptions, not proof of
premature reuse or a confirmed repair of the firmware's hardware-status wait.

Tests check descriptor DMA addresses, unchanged previous-slot bytes over
4,300 transmissions and ring wraps, undersized/overflowing DMA storage, and
allocation failure/cleanup for the larger block. Physical-card retesting is
required.

## Review of the supplied mac80211 references

The newly supplied `mlme.c`, `tx.c`, `rx.c`, `status.c`, `wme.c`, `util.c`
and headers expose configuration normally supplied above iwlegacy. The
following are source-level differences, not proven causes of the firmware
hardware-status stall. No runtime changes were made for this review.

### BSS protection and association parameters

`mlme.c:ieee80211_handle_bss_capability` derives protection from the ERP IE
and lets its Barker-preamble bit override the capability's short-preamble
bit. `iwlegacy/common.c:il_mac_bss_info_changed` maps these decisions into
RXON TGG protection, self-CTS and short-preamble flags. mac80211 TX also
supplies CTS/RTS policy to the rate-control/driver path.

The native scan parser does not retain the ERP IE. Native RXON uses the
cached beacon capability bits, and native TX never requests RTS/CTS
protection. Association-response handling reads status and AID but does
not consume its capability or information elements. This can leave native
radio configuration inconsistent with negotiated BSS parameters. A repair
should parse and validate those inputs before changing radio/protection
flags; setting CTS unconditionally would not reproduce Linux policy.

### Non-WMM operation and queue configuration

`util.c:ieee80211_set_wmm_default` provides uniform legacy contention
parameters when QoS is disabled. Its minimum contention window depends on
11b versus 11g operation, not merely the 2.4 GHz band. The native driver
always uses 31 on 2.4 GHz and sends C_QOS_PARAM with flags=1, even though its
association request does not negotiate WMM. In contrast,
`iwlegacy/common.c:il_update_qos` sets UPDATE_EDCA only when QoS is active.

`wme.c` routes ordinary non-QoS data to mac80211 AC_BE, while the native
driver sends all management and data on queue 0. The access-category enum,
firmware EDCA table order, and scheduler FIFO mapping are distinct: for
example, `il_mac_conf_tx` reverses category order when writing the firmware
table. A queue change must follow the complete mapping, not assume that
mac80211 AC numbering equals firmware EDCA numbering. Uniform native
parameters reduce some differences, and none establishes the fatal stall.

### Receive processing and connection monitoring

`rx.c:ieee80211_rx_h_check_dup` checks retry/sequence state before delivering
duplicate frames. The native open-network receive path has no equivalent
duplicate suppression; CCMP replay handling only covers protected traffic.
This is a data-path correctness gap rather than evidence of a firmware hang.

`mlme.c` separates beacon and connection monitoring and chooses probing
according to advertised TX-status capabilities. Native station handling
uses one last-receive timestamp and a 30-second timeout, updated for data
before its decrypt/validation succeeds. Better monitoring should preserve
distinct beacon, validated-data and acknowledged-TX evidence. Missing probes
do not explain a firmware NMI inside its hardware wait loop.

### Reference completeness and next implementation order

`af_ieee802154.h` is the IEEE 802.15.4 userspace interface, not the requested
Wi-Fi protocol header. Still needed: `include/linux/ieee80211.h` from the
same kernel revision as the other references, plus the source tag/commit.
If that revision splits definitions into additional headers, include those
referenced protocol headers too.

Prioritize consistent legacy/non-WMM configuration and validated BSS/ERP
parameters, then queue-mapping verification. Add duplicate suppression and
connection monitoring as separate changes with their own tests. Preserve
software CCMP, controlled-port gating and matched TX completion ownership.
Keep physical-card firmware-stall testing separate from ordinary connection
timeout/retry tests.

## Implemented mac80211 contract repairs and station split

The follow-up implementation now parses shared rate/ERP IEs for scanning and
association, validates association-response capabilities and optional supported
RSN, and derives preamble/protection flags from ERP. Validated beacon changes
use C_RXON_ASSOC rather than a full retune. OFDM transmissions use CTS/full-TXOP
protection when the selected BSS requires it.

The station remains non-WMM: EDCA UPDATE stays disabled, legacy contention
parameters distinguish 11b-only from OFDM peers, and its single in-flight queue
is now best effort (host queue index 2). Historical crash captures above still
describe queue 0; new TX snapshots encode queue 2 in their sequence field.

Unicast retry duplicates are suppressed per TID, with history committed only
after validation and reset at disconnect. Monitoring tracks beacon reception,
validated data and acknowledged unicast TX separately. Missing evidence starts
bounded null-data probing before link loss. Invalid crypto/EAPOL and duplicates
do not refresh the data timestamp. Firmware SW_ERR remains a separate fatal
path and is never hidden by monitor retries.

The station lifecycle is split into focused files under `station/`, with shared
BSS and RSN parsers at the iwlegacy root. See the driver's README developer map.
Tests cover radio flags, malformed IEs, non-WMM commands, best-effort doorbells,
OFDM protection, duplicate/TID isolation, live ERP updates, successful and failed
probes, monotonic wrap and invalid-crypto liveness. These checks validate the
host implementation; the physical firmware NMI still needs hardware retesting.

## Follow-up: immediate -5 after authentication

A reported -5 immediately after authentication is not enough to distinguish
firmware SW_ERR from host association validation. The preceding implementation
added a rejection gate requiring association-response ESS/IBSS/Privacy bits to
agree with the scanned BSS. The supplied Linux response path does not use that
gate. It has been removed; response capabilities still configure radio flags,
while the selected security, optional RSN validation and actual WPA2 handshake
continue to gate ordinary traffic. A successful response with omitted capability
bits is now covered by a complete WPA2 integration test.

AID decoding now masks the five reserved upper bits, following the supplied
mac80211 response handler, before validating 1..2007. A response with raw AID
0xf801 is tested to produce station AID 1.

Connection failures now retain their phase, host rejection reason, AP status,
raw response capability/AID, TX snapshot, IRQ/FH state and firmware error table
before station clear/DMA teardown. Diagnostics format 4 exposes these fields.
This corrects an introduced compatibility restriction and an evidence-retention
gap; it does not establish the cause of the user's new failure without the
hardware status snapshot. Malformed IEs and AP rejection still fail explicitly.

### Supplied hardware snapshot and PCIe power workaround

The follow-up hardware snapshot reports last command 0x47 (not pending or
poisoned), IRQ/FH 0x82000008/0x00010000, firmware SYSASSERT 5, assertion PC
0x8b6, blink2 0x274, ilink1 0x31c and NMI PC 0x3560. SW_ERR is set: this
is a firmware fault, not merely rejection by the new host association parser.
The NMI is again in the same hardware-status wait loop. A completed 0x47
identifies the last setup command, not a proven offending command.

A separate missing Linux workaround was found in 3945-mac.c's PCI probe:
`pci_disable_link_state(... L0S | L1 | CLKPM)`. Native setup disabled firmware
sleep and internal NIC L1 activation but did not clear PCIe link power controls.
New `pci_power.c` clears ASPM/clock-PM bits on the adapter and immediate PCIe
parent before bus mastering, preserves unrelated control bits, and verifies
readback. Tests cover both link partners, exclusion of unrelated ports and
readback failure. Diagnostics expose before/after values so the next physical
run can distinguish an applied change from an already-disabled link.

This is an evidence-backed setup difference and a repair attempt, not a proven
explanation of the internal firmware bit or a confirmed fix of the stall. If
power bits were already clear, this workaround will not change that card's
link configuration. The association compatibility corrections above remain
useful but do not explain this supplied SW_ERR record.

## Follow-up: unchanged link state and TX sequence 0x0207

The next hardware run reports endpoint/parent Link Control 0x0040 before and
after the workaround. ASPM and clock-PM were already clear on both partners,
so that change did not alter this card's link settings. Error -5 and the prior
firmware signature persist. Last TX is 0x0207, length 76, flags 0x2008, rate
0x0a, station 0 and firmware security 0. This again fits the earlier 76-byte
software-CCMP ARP hypothesis, but length alone cannot establish packet type.

Station phase 5 previously covered both WPA2 setup and an already-open data
port. Diagnostics format 5 now saves WPA phase/authorization/port-open flags,
EtherType, frame control, DMA addresses and eight TX submission/completion
records before teardown. This should distinguish handshake traffic from the
first ordinary ARP/IP transmission and show preceding completion statuses.
Firmware event-log capture remains outstanding; host TX history is not a
substitute for the firmware event log.

A separate sequencing defect was reproduced: an AP supplying AssocResp and
EAPOL M1 in one RX batch causes native M2 transmission before associated RXON
has been programmed. The new register-model test fails on that ordering before
the repair. RX processing now yields at association success and preserves the
remaining deferred frames until configuration finishes. The same WPA2 exchange
then passes, with both M2 and M4 submitted after associated radio setup. This
fix is not established as the explanation for the reported 76-byte TX stall.
