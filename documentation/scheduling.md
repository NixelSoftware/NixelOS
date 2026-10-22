# CFS scheduler

The kernel uses the single-CPU CFS implementation in `src/kernel/scheduler.c` and `scheduler.h`. `task.c` handles task creation, stacks and architecture context switching; it delegates CPU selection and accounting to the scheduler. The Makefile links both into the kernel.

The policy follows the [Linux CFS design](https://www.kernel.org/doc/html/v6.2/scheduler/sched-design-CFS.html): a red-black run queue ordered by weighted virtual runtime, a monotonically advancing minimum virtual runtime, and selection of the leftmost runnable entity. The running task is kept outside the tree. Nice values from -20 through 19 use the conventional CFS weight table; normal priority has weight 1024.

CPU time is measured in nanoseconds using a TSC calibrated against PIT channel 2, with a latched PIT sub-tick fallback when TSC is unavailable. The 1 kHz timer drives preemption; time accounting and the legacy clock do not rely on counting delivered IRQs when TSC is available. Time spent in short syscalls and tasks that sleep before a timer tick is accounted, rather than counting timer interrupts as CPU usage. Virtual time advances by execution time multiplied by 1024 and divided by task weight. The scheduler targets a 4 ms scheduling period, expanding that period when needed to give each runnable task at least a 1 ms scheduling granularity. Actual slices are proportional to runnable weight.

Timer interrupts preempt user code, kernel threads and syscall work. Only explicitly disabled preemption or IRQs defer a switch. New and waking tasks re-evaluate the current task's slice against the updated runnable load and can request preemption immediately. Task creation honors that request at a safe preemption point. A waking task can also request preemption when it is sufficiently behind the current task; wakeup granularity is 0.5 ms. Sleep credit is limited to 1 ms of weighted execution, so sleeping cannot accumulate unlimited priority. Equal-runtime ordering rotates through an enqueue sequence. A separate idle task halts only when no normal task can run and is excluded from runtime/load accounting.

## Service isolation and locking

The desktop, network pump, startup-audio producer and address-space reaper have independent scheduled contexts. Their nice values are -5, -2, -4 and +5 respectively. Application tasks default to nice 0. Executable loading is queued to a worker: the WM callback copies the request and returns, and disk reads occur without holding the core state lock.

Shared kernel state uses recursive sleeping mutexes. The core state lock serializes WM/network/lifetime changes; filesystem/ATA, audio and floppy operations have separate locks. Contenders block atomically and are handed ownership before waking, without spinning or losing wakeups. Owners remain preemptible. Priority inheritance temporarily raises a lock owner's weight when a higher-priority waiter needs it, propagates through waiting owners, and restores the appropriate weight on release. IRQ handlers do not acquire sleeping mutexes.

Each task retains its kernel stack, integer registers, x87 state, address space and user-return frame. CR3 and its software mirror change atomically with IRQs disabled. Application mappings remain pinned while an application is running. Small DMA/PIC updates still mask interrupts briefly. ATA busy waits park the filesystem owner when scheduling is active; they do not hold the core/WM lock.

The compositor coalesces application present requests and paces redraws at approximately 60 Hz. Input is serviced every 2 ms, independently of network and audio pumping. Background terminal output preserves edited commands and window focus. Network receive work and deferred ARP packets remain bounded. Sequential WAV reads reuse FAT cursors.

Framebuffer presentation has its own `display` task at nice 0. WM paint and cursor calls enqueue a merged damage rectangle and return. The worker snapshots changed pixels and cursor state into private physical pages while briefly holding the core lock, then releases that lock before writing VRAM. Only the worker owns those snapshot pages. Requests arriving during a transfer remain queued and coalesce into the next update. Slow device writes therefore do not keep the shared kernel lock held. Boot and panic output can still present synchronously before scheduling or with interrupts disabled.

The `shell` task, also at nice 0, consumes a bounded eight-command queue. Enter in Terminal copies the command and returns to input processing. Filesystem commands execute on the worker without retaining the WM/core lock. Their output and clear callbacks reacquire the core lock only to update terminal state and preserve commands being edited. Viewer file reads also run without the core lock; decoded image state is published under the lock.

The application ABI retains its 100 Hz tick counter. `SYS_YIELD` sleeps for 1 ms, allowing other work to run; synchronous DNS waits park only the calling task. Startup audio/network work runs independently of the WM instead of being invoked recursively by yielding applications.

## Scope and validation

This is a single-CPU scheduler for NixelOS, not a Linux kernel port. It does not implement SMP balancing, scheduling groups, real-time classes, or SSE/SIMD enablement. Scheduling cannot make an indefinitely masked interrupt or privileged code that never releases a required lock safe. Lock serialization and hardware speed can still affect frame latency; there is no hard real-time guarantee.

Validation runs from temporary files, respecting the preference to keep tests outside the repository. Host checks cover red-black invariants across 200,000 randomized queue mutations, equal/weighted CPU shares, monotonic virtual-time floors, sleeping/wakeup and idle transitions. Separate checks exercise mutex blocking, ownership handoff, recursion, inherited/restored priorities and scope cleanup. The production i386 64/32 divider is checked against 1,000 independent reference quotients. QEMU validation covers simultaneous CPU-bound user and kernel applications, audio, PING, console input, window dragging, calculator rendering, user-fault recovery, and private-memory/x87 state across switches.

A TSC-disabled QEMU boot also completed startup playback using the PIT fallback, with no slow DMA blocks or refill stalls. The final mixed kernel/user stress run completed `o98.wav` in 790 legacy ticks (7.90 s) against a 7.858 s expected duration, with zero late processing chunks or DMA refill stalls.

## Graphics and streaming latency

The WM composites retained user surfaces in page-sized row spans under the compositor lock. Composition does not request another application redraw. Creation sends the initial redraw event, and dropping a dragged window sends one event with its final position.

Dragging caches the background without the moving window and a complete image of that window. Subsequent motion restores the old footprint, copies the retained image to the new position and presents only the affected screen region. Overlapping footprints are merged. Content, focus, visibility and other scene changes invalidate the cache; a window that is initially clipped uses full composition until a complete image can be captured. Motion is clipped against the screen and taskbar. Cursor movement restores and paints only the cursor footprints. This removes repeated legacy draw callbacks and full-screen VRAM copies from ordinary drag frames.

Framebuffer fills use clipped bulk stores. The REP assembly declares its modified registers, and rectangle blits use a fresh count for each row. Boot prefers an 800x600 32-bit VBE mode for direct copying; 24-bit remains a fallback. Two fixed drag caches consume approximately 3.7 MiB of kernel memory; the linker rejects globals that would overlap the backbuffer at 0x00A00000.

ATA transfers use a sector-sized `rep insw`/`rep outsw` operation instead of issuing 256 separate C-level word transfers. The filesystem mutex preserves command ownership, and interrupts remain enabled while transferring samples and disk data.

The SB16 driver has an eight-block, 128 KiB DMA ring below 16 MiB, aligned within one ISA DMA page. Full blocks use auto-initialized DMA and DSP command B6h so hardware crosses block boundaries continuously. Normal full-block IRQs acknowledge completion, account bytes and release slots; they do not copy samples or restart the DSP. Short writes use exact-length single-cycle transfers. End-of-stream, short-tail and underrun transitions may reprogram DMA. Startup prefill waits for two blocks or at most 20 ms. Producers reserve a free slot, copy with interrupts enabled, then publish it atomically; an IRQ cannot consume a slot still being filled.

`SYS_AUDIO_DRAIN` (34) marks end-of-stream after the last successful write. The DSP receives D9h before the final full block ends, or before a known short tail, and the caller polls status until the queue drains. Further writes are rejected after drain. An empty queue before end-of-stream can recover when the producer supplies more data. See the [Creative programming guide](https://pdos.csail.mit.edu/6.828/2005/readings/hardware/SoundBlaster.pdf) and [QEMU's SB16 implementation](https://github.com/qemu/qemu/blob/master/hw/audio/sb16.c); auto-init counts 16-bit samples, whereas QEMU's single-cycle stereo path expands its count by both sample width and channels.

The WAV player keeps full output blocks queued and converts sources above 44.1 kHz to 44.1 kHz using streaming linear interpolation. Fractional position and the preceding stereo frame survive filesystem read boundaries. This avoids the observed QEMU 8.2.2 behavior where requesting 48 kHz directly played samples at 45 kHz: a 12-second tone took roughly 12.8 seconds and its 440 Hz component became 412.5 Hz. Conversion restores the intended duration and pitch. This is a linear converter, not a studio resampling filter.

Render counters (`wm_render_stats`) record composition count, elapsed composition/enqueue time, maximum time and compositions exceeding 16.67 ms. With asynchronous presentation enabled these counters exclude the display worker’s device-transfer time. Scheduler information includes cumulative idle time. These debugger diagnostics include preemption during a frame.

## Latency validation

Temporary tests stay outside the repository. A matched before/after comparison used the prior renderer/audio path and the current implementation with QEMU TCG, one virtual CPU, 128 MiB RAM, e1000, SB16 and WAV output capture. It played a generated 12-second 44.1 kHz, 16-bit stereo tone, ran PING, verified an active Calculator title-bar drag and sent 950 mouse movements at approximately 100 Hz over 10 seconds.

| Measurement during drag | Previous implementation | Current implementation |
| --- | ---: | ---: |
| Frames per second | 59.9 | 60.0 |
| Mean render duration | 6.71 ms | 0.75 ms |
| CPU busy | 46% | 12% |
| Frames over 16.67 ms in measured interval | 0 | 0 |
| Late DMA blocks / refill stalls | 0 / 0 | 0 / 0 |

The improvement is CPU and frame-time headroom; the previous build also reached 60 FPS on this host. A 48 kHz source converted to 44.1 kHz averaged 0.73 ms per render at 60 FPS, with zero late DMA blocks or refill stalls. Recorded 44.1/48 kHz source tones had approximately 12-second durations, the expected 440 Hz component, and no interior discontinuities or sustained zero runs under the waveform checks. PING completed and Calculator accepted `2+3=5` after the drag. WAV capture does not measure the user's live audio/display backend or end-to-end input latency.

Host checks compare cached dragging against full composition pixel-for-pixel, including overlapping windows, offscreen motion, taskbar clipping and cache invalidation. Driver checks cover full-ring backpressure, continuous wraparound without DSP restarts, exact short tails, drain, spurious IRQs, and completion while the producer copies. A randomized mixed-size sequence verifies FIFO sample identity and byte accounting. Independent rational interpolation checks cover stereo extrema, input/output chunk boundaries, short sources and queue retries. Existing WAV-format and startup-audio checks continue to pass.

## QEMU configuration

The supplied QEMU command uses [TCG software emulation by default](https://www.qemu.org/docs/master/system/invocation.html). `make run` retains TCG for portability and configures one CPU. On Linux with an accessible `/dev/kvm`, use `make run QEMU_ACCEL=kvm`. The kernel uses one processor, so extra `-smp` CPUs will not distribute work. These workloads fit within 128 MiB. The benchmark environment had no `/dev/kvm`; KVM performance and the user's host backend have not been measured.

## Input progress during playback

The earlier playback benchmarks used silent/WAV output and began interacting after startup sound. That left startup input and the graphical backend insufficiently checked. A GTK test exposed frame stalls up to 224 ms in the synchronous presentation path. The current worker design separates VRAM transfers and shell command waits from WM input processing.

Temporary production-code checks cover asynchronous damage enqueueing, fragmented snapshot pages, stable snapshots when another producer changes the scene, cursor clipping, and packed/padded 24/32-bit output. Shell checks verify that input submission performs no disk reads, that disk callbacks run without the core lock, and that background output/clear preserve an edited command. CFS, mutex, cached composition and DMA queue checks continue to pass.

QEMU checks now send commands within the first second after the scheduler starts, while the startup DMA stream is active, and repeat command input during `sound o98.wav`. They inspect live task states, lock ownership, audio completion counters and screenshots. Testing includes the default host audio backend and a GTK window, rather than relying only on silent or WAV capture. This validates concurrent progress; it does not promise a hard latency bound for arbitrary privileged callbacks or a blocked host process.

The build creates a temporary image and atomically replaces `os.img`. A running VM keeps its existing disk inode and in-memory kernel; restart QEMU after rebuilding to execute the new kernel.

## Serial frame timings

`make run` sends `[FRAME] render` and `[FRAME] present` summaries to serial stdio at most once per second per active stage. Every completed frame is counted, including fast frames. `count`, `slow`, `avg_total_ns`, `max_total_ns` and `max_wait_ns` cover work since the previous summary; slow means total latency exceeded 16,666,667 ns. Idle stages report their accumulated samples when work resumes. Durations are decimal nanoseconds (divide by 1,000,000 for milliseconds). Serial output occurs after measurement and can still perturb subsequent work. Frame summaries do not interleave with each other; unrelated serial diagnostics can still interrupt them.

Presentation adds `max_snapshot_ns`, `max_vram_ns`, `max_cpu_ns`, `max_offcpu_ns`, `max_snapshot_cpu_ns`, `max_vram_cpu_ns`, and `max_lock_ns`. Snapshot time now includes gaps between bands, including mutex reacquisition; the lock maximum measures one band's lock-held work, including preemption. Runtime is scheduler-accounted time; off-CPU time is drawing elapsed time minus scheduled runtime. Interrupt service and host/QEMU pauses while the task is current can be charged as runtime. This is not a hardware CPU profiler. Maxima may come from different frames and must not be added together.

`scanned_pixels` counts requested pixels examined across all batches; `copied_pixels` counts row spans actually written to VRAM. The worker compares up to 64 pixels at a time against its retained private image, updates changed chunks, and transfers only each changed row's first-to-last changed span. Identical repaint requests generate no VRAM writes after the initial full presentation. The comparison includes the cursor overlay so old cursor footprints are restored correctly. Snapshot storage remains private to the display worker and handles fragmented physical pages.

The scene lock is released every eight snapshot rows. Writers can progress between bands and damage arriving during a batch remains queued. Each band sees a consistent scene, but the complete screen can contain bands from successive scene versions until the next batch catches up. Presentation still has no vertical-sync guarantee. VRAM transfers run outside the scene lock and retain stable snapshots even if producers redraw the backbuffer. Present completion means the CPU finished writing VRAM, not monitor scanout.

Temporary production-code checks cover zero-write identical repaints, sparse changes, page boundaries, cursor overlay/clipping, stable retained snapshots, padded and packed 24/32-bit framebuffer output, decimal 64-bit formatting, summary aggregation, rate limiting and busy-logger sample retention. A one-CPU headless QEMU TCG check entered HELP during startup playback, dragged Terminal by its title bar, and launched Calculator. Two drag summary windows averaged 8.70 ms and 8.38 ms total presentation latency, with one slow batch out of 121 (22.21 ms maximum). Their maximum measured band lock times were 0.54 ms and 0.50 ms. Startup audio completed with zero slow blocks or refill stalls. These observations describe this test host and are not matched before/after or live display/audio latency guarantees.

## Event-loop gaps and serial overhead

Completed-frame statistics cannot establish continuous responsiveness: the event loop may stop requesting frames. `[INPUT]` summaries now count every WM poll and handled key/mouse event, and report `max_gap_ns` between poll starts, `max_work_ns` after acquiring the scene lock, and `max_lock_wait_ns`. `at_ns` timestamps correlate input and frame summaries. A gap includes prior processing, the normal 2 ms sleep, logging and scheduler/host delays; it is not automatically a whole-system freeze.

After tasking starts, serial producers enqueue into a 16 KiB ring without waiting on the UART. A nice +10 worker checks readiness once per wake and writes at most one 16-byte FIFO load. `serial_dropped_bytes` is a debugger-visible overflow counter. Boot output retains bounded polling; panic and power paths flush the queue and switch back to synchronous output before scheduling stops. Summary formatting still costs CPU, but input/scene-lock owners no longer wait for UART transmission.

Graphical GTK tests with both host audio and silent audio still showed occasional input-loop gaps in the tens of milliseconds, including a roughly 95 ms gap with buffered serial. Input commands and startup playback progressed concurrently, but those tests do not resolve the reported startup freeze or establish host audio as its cause. Use the new input progress records to distinguish missing draws from fast completed draws on the affected host.

For the memory recorder, workload isolation profiles, capture commands and measured acceptance status, see [Startup latency tracing](startup-tracing.md).
