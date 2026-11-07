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
   storage per in-flight descriptor. The native driver reuses a single buffer
   after a matching TX response. That can be valid for single-inflight TX, but
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
