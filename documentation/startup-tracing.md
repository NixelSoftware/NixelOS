# Startup latency tracing

The remaining issue is a gap in input/event-loop progress, not necessarily a slow completed frame. This recorder measures before changing scheduling policy. It is single-CPU instrumentation for the kernel's existing uniprocessor implementation.

## Build and capture

Normal builds default to `STARTUP_TRACE=0 STARTUP_PROFILE=0`; trace hooks compile out. To record a normal startup:

```sh
make STARTUP_TRACE=1 STARTUP_PROFILE=0
make run STARTUP_TRACE=1 STARTUP_PROFILE=0 QEMU_EXTRA='-qmp unix:/tmp/nixel-startup.qmp,server=on,wait=off'
```

Restart QEMU after rebuilding. Do not rebuild the image or replace the trace ELF between boot and capture. From another terminal, after approximately 12 seconds of guest uptime:

```sh
python3 tools/startup_trace.py capture \
  --qmp /tmp/nixel-startup.qmp \
  --elf src/kernel/kernel.trace.elf \
  --output /tmp/nixel-startup \
  --observe-seconds 1
```

The capture briefly stops the VM to read a consistent snapshot, then resumes it if it was running. The script saves the matching ELF, trace metadata, page map, raw records, JSON analysis and chronological CSV. `--observe-seconds` adds an approximate comparison of the scheduler's sampled guest clock against host monotonic time, outside that pause. It is not an end-to-end input-latency measurement.

Reanalyze without running QEMU:

```sh
python3 tools/startup_trace.py analyze /tmp/nixel-startup --elf /tmp/nixel-startup.elf
```

The ELF must match the booted image. A stale image/ELF pair is rejected when its metadata does not match. For repeated builds, use an isolated checkout or source copy and keep each image paired with its ELF. `kernel.trace.elf` is retained only for trace builds; boot-image size and fixed backbuffer overlap checks still apply.

The first 12 seconds after scheduler clock startup are quiet: ordinary serial producers enqueue without polling, and the serial worker does not transmit until capture ends. The static boot serial ring also accepts messages before tasking exists. Queue overflow drops bytes and increments `serial_dropped_bytes`; it never switches to synchronous output. Panic and power paths explicitly stop capture and restore synchronous output. The actual UART configuration is **38,400 baud**, not 115,200.

## Isolation profiles

All profiles perform the existing hardware/desktop initialization before entering their isolated runtime. These are workload-isolation modes, not stripped-down alternative kernel binaries.

| STARTUP_PROFILE | Runtime |
| --- | --- |
| 0 | Normal desktop, audio, display, networking, shell and reaper |
| 1 | Input polling and sleep/wakeup only; no rendering, audio or networking service |
| 2 | Normal services except the startup audio task |
| 3 | Idle heartbeat and sleep/wakeup; no input-device polling or rendering |

A serial worker remains present but does not transmit during capture. Build each profile with `STARTUP_TRACE=1`. Change one host setting at a time using `QEMU_EXTRA`: for example `-display none`, or `-icount shift=auto,align=off,sleep=on`. Keep CPU count, audio backend, memory and workload identical for matched comparisons. Use `QEMU_ACCEL=kvm` only on a host with usable `/dev/kvm`.

`-icount` is a time-model experiment, not a production latency fix. Passing a guest-time threshold with instruction counting does not establish that host-visible input or graphics never paused.

## Recorded data

`startup_trace_state` is a debugger-visible 4096-byte metadata block. `startup_trace_pages` lists the physical pages of CPU 0's fixed-size ring. Trace storage reserves up to 2048 physical pages (8 MiB) before capture, so it does not overlap the fixed graphics backbuffer or allocate in IRQ paths. If memory is limited, capacity falls back to a smaller power-of-two page count. Total allocation failure disables capture and is reported by the tool.

Each 32-byte little-endian record contains a nanosecond timestamp, event ID, task ID, three arguments, CPU index and sequence number. Writes disable local interrupts briefly using raw helpers so the recorder cannot recursively trace itself. There are no allocations, sleeping locks, formatting or device writes in record production. The ring reports overwritten records; missing intervals must not be classified as complete evidence.

Events cover:

- Task switch out/in, target task states and preemption count at switch-out.
- Timer IRQ entry/exit, including switches taken inside the IRQ.
- IRQ-off regions entered through the common task IRQ helpers, their caller addresses and durations. Short regions contribute to the maximum; regions at least 100 microseconds also get individual records.
- Mutex acquisition/release, recursion depth, wait start/resume, lock address and owner.
- UART bytes enqueued/dropped and actual transmissions tagged synchronous or queued. Synchronous byte duration includes polling; queued duration covers the port write.
- RTC logical register reads (select plus data access) and PS/2 status/data reads and command writes, with elapsed durations.
- Input poll entry/exit with scheduler runtime, plus RTC, input polling, event handling and render phase boundaries.

Clock metadata records whether timestamps use the TSC calibrated against PIT channel 2, or the PIT fallback, and the calibrated cycles per microsecond. Events before scheduler clock initialization have timestamp zero and provide order/counts only. The PIT fallback cannot recover all time lost to coalesced timer delivery; use the host-clock comparison and report this limitation.

IRQ-off tracking closes a task's interval at a switch: a sleeping task retaining a CLI frame does not mean the CPU kept interrupts disabled while other tasks ran. The assembly timer path has separate entry/exit events. Boot, panic and terminal power-off CLI paths are outside interactive capture; raw assembly return sequences and recorder-internal IRQ masking are not individually attributed by the helper tracker. Timer delivery and handler spans provide complementary evidence. Caller addresses can be resolved against the saved ELF.

## Reading the report

The full capture histogram uses 100-microsecond gap bins, with overflow at 100.1 ms. P99 is reported as a bin upper bound, not a falsely precise percentile. Maximum gap remains exact to the selected guest clock.

For every retained poll-to-poll gap over 10 ms, JSON includes scheduled runtime versus elapsed time, CPU occupancy by task, task sleep/block/wake states, lock owners, overlapping long IRQ-off/IRQ-handler regions, timer-delivery gaps, UART byte counts, RTC/PS/2 counts and long accesses, and input phase spans. Long I/O records also include overlap with that task's scheduled intervals: a preempted `inb` call must not be mistaken for a slow device operation. `unclassified_gaps` makes missing history explicit.

Interpretation:

- Large runtime with a long phase suggests investigating that phase; host pauses while the task is current can still be charged as runtime.
- Low runtime while another runnable task occupies the CPU suggests scheduler/preemption investigation.
- A matching lock-wait interval identifies the owner to investigate.
- An idle-dominated gap with a matching missing timer interval and no matching traced IRQ-off region points toward timer delivery or host/emulator timing. It does not, by itself, prove a particular host component is responsible.
- UART output can only explain a polling stall if actual synchronous transmissions overlap it. Quiet captures deliberately remove transmission as a variable; queued formatting still consumes CPU.

Do not add scheduling classes, IRQ-driven input or new locking schemes just because the component exists in another OS. First identify which interval accounts for the missing time, then change that path. Retest with tracing disabled as well: recording every device access and task transition adds measurable overhead.

## Results and acceptance status

Initial twelve-second diagnostic runs on QEMU TCG, one CPU and 128 MiB RAM produced the following results. They are workload samples, not hard latency guarantees or measurements of monitor scanout.

| Runtime / host setup | Maximum input/heartbeat gap | P99 bin upper bound | Gaps over 10 ms |
| --- | ---: | ---: | ---: |
| Normal startup, graphical, host audio | 910.47 ms | 30.2 ms | 86 |
| Input-only, graphical, host audio | 123.58 ms | 3.5 ms | 4 |
| Input-only, headless, same host audio backend | 6.43 ms | 3.3 ms | 0 |
| Idle heartbeat, headless, silent backend | 3.84 ms | 3.3 ms | 0 |
| Input-only, graphical, host audio, `-icount` | 4.63 ms guest time | 3.3 ms guest time | 0 |

The graphical input-only run's largest gap contained about 0.036 ms of desktop runtime and 123.54 ms attributed to idle, alongside a 123.55 ms timer-delivery gap. No matching helper-traced IRQ-off region or long device access accounted for it. The normal capture retained no synchronous UART transmissions during the measured window. These observations weaken UART starvation and CFS policy as explanations for that specific gap and implicate the emulator/host timing path. They do not establish that every startup pause has the same cause.

A graphical idle comparison disconnected before completion and a later comparison used an image/ELF mismatch; both were discarded. Subsequent matched tests used an isolated build directory. KVM could not be tested because this environment has no `/dev/kvm`.

The normal graphical startup **does not meet** the requested maximum <10 ms / p99 <5 ms input criteria. Earlier audio tests showed zero refill stalls in some runs, but that does not prove responsive input. Normal graphical frame-budget and audio acceptance must be checked together with host-visible input/graphics latency after addressing the host/emulator path. Do not claim the startup freeze is fixed from fast completed-frame statistics or from the `-icount` result.

Temporary checks validate the ring layout, wraparound and overflow, page capacity behavior, IRQ/sleep interval handling, histogram and capture completion, trace parsing, CPU occupancy, lock-owner attribution and preempted-I/O classification. Existing scheduler fairness/randomized checks and asynchronous UART queue checks also pass. Keep tests and captured binary artifacts outside the repository.
