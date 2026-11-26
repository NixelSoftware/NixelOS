# Codebase Audit: Performance and Architecture

Written for: the NixelOS maintainers, as a prioritised backlog.

## Scope and method

- **In scope:** first-party kernel (`src/kernel`), boot (`src/boot`), system services and window manager (`src/system`), the build (`Makefile`, `build.py`, `tools/`).
- **Out of scope:** vendored code (`wolfssl`, `jitterentropy`, `minimp3`, `syndig/upstream`, `doom`), generated data, and `archive/`.
- **Method:** static reading of the hot paths: boot, scheduler, timer and IRQ, mutexes, syscalls, the render loop, the filesystem, networking, and the build. Nothing here has been profiled on hardware or in QEMU. Treat the costs as reasoned estimates until measured.

Each item gives a severity (**High / Medium / Low**) and a location. Line numbers are from the tree at commit `62808c4`.

---

## 1. Performance issues

### P1. Per-byte page-table walks on user strings and buffers (High)

`user_readable()` walks the page directory and page table on every call. Several syscall paths call it once per byte:

- `copy_user_text()`: [src/kernel/syscall.c:246-253](src/kernel/syscall.c#L246-L253) calls `user_readable(address+i, 1)` for each character.
- `SYS_WINDOW_DRAW` text copy: [src/kernel/syscall.c:802-806](src/kernel/syscall.c#L802-L806) does the same for every glyph of a text draw.
- Filename and hostname copies: [src/kernel/syscall.c:338](src/kernel/syscall.c#L338), [495](src/kernel/syscall.c#L495), [565](src/kernel/syscall.c#L565), [932](src/kernel/syscall.c#L932).

**Impact:** every printed string or drawn label costs a page-table walk per character. Text-heavy apps (terminal, browser, mail) pay this constantly.

**Fix:** validate the range once per page, then copy with a bounded loop. Better still, validate the whole span once and copy in chunks.

### P2. One mutex acquisition per pixel (High)

[src/kernel/main.c:155-161](src/kernel/main.c#L155-L161) `api_put_pixel()` takes `kernel_mutex` for each pixel. The same applies to `display_put_pixel` ([src/kernel/drivers/display.c:207-211](src/kernel/drivers/display.c#L207-L211)).

**Impact:** a user app that draws pixel by pixel pays a lock, IRQ-save and possible context switch per pixel. This also keeps the compositor waiting.

**Fix:** remove the per-pixel API from the public table, or add batched calls (`fill_rect`, `blit_span`) and make the app use those. `fill_rect` already exists.

### P3. Full-screen repaint and blit every frame (High)

`wm_render_screen()` ([src/system/graphics/wm/presentation.c:13-46](src/system/graphics/wm/presentation.c#L13-L46)) always does a full pass:

1. `desktop_wallpaper_draw()` writes every pixel of the screen.
2. `desktop_draw()` and `wm_draw_windows()` redraw everything, including windows that did not change.
3. `display_blit()` copies the whole backbuffer to the framebuffer.

There is no dirty-region tracking. A cursor move or a clock-minute change repaints about 3 MB at 1024×768×32.

**Impact:** most of the frame budget goes to work that is identical to the previous frame. This also explains why the code tracks `over_budget` frames.

**Fix:** track dirty rectangles per window and per desktop item. Repaint only the union of damaged regions. Keep the full-frame path for resizes and wallpaper changes.

### P4. Wallpaper drawn per pixel with division and indirection (High)

[src/system/graphics/wm/desktop_wallpaper.c:176-191](src/system/graphics/wm/desktop_wallpaper.c#L176-L191) computes source coordinates per pixel. It has real divides (`x * SCREEN_W / SCREEN_WIDTH`, `y * SCREEN_H / SCREEN_HEIGHT`, and `x % SCREEN_W` in tile mode) and an indirection through `allocated_frames[]` for every pixel. That is roughly 786k pixels per frame at 1024×768.

**Fix:** precompute a scaled wallpaper once when the mode or resolution changes, and cache it as a backbuffer-sized surface. The per-frame cost then becomes a `rep movsl` per row. This also makes P3 cheaper.

### P5. NICs are polled, not interrupt-driven (High)

The network stack polls every adapter on a timer. [src/kernel/main.c:397](src/kernel/main.c#L397) runs `sys_net_poll(); ntp_poll(); task_sleep_ms(2);` in a loop. `e1000`, `r8169`, `b44`, `tg3` and `pcnet` all expose `*_poll()` and none installs an IRQ handler.

**Impact:** RX latency is up to 2 ms, and throughput is capped by how often the task runs. CPU is also burned on empty polls while the NIC is idle.

**Fix:** register the NIC IRQ, drain RX in the handler (or a deferred bottom half), and keep polling only as a fallback when no IRQ line is available.

**Status:** fixed in code. `e1000`, PCnet, `r8169`, `b44` and `tg3` interrupt ([src/kernel/irq.c](src/kernel/irq.c)): the handler masks the device and wakes the network task, which drains RX. Only `e1000` and PCnet have been tested, in QEMU. The `r8169`, `b44` and `tg3` interrupt paths are untested and need hardware. With a working interrupt the task still wakes at least every 10 ms for TCP, ARP and NTP timers; an adapter on an unsupported IRQ line (including IRQ 5) is still polled. Wi-Fi is still polled.

### P6. Polling loops with fixed sleeps (Medium)

Several services wake on a fixed period regardless of work:

- Desktop loop: [src/kernel/main.c:447](src/kernel/main.c#L447) `task_sleep_ms(2)`, about 500 wakeups per second even when idle.
- Audio: [src/kernel/main.c:401](src/kernel/main.c#L401) `task_sleep_ms(2)`.
- USB: [src/kernel/main.c:409](src/kernel/main.c#L409) `task_sleep_ms(10)`.
- `task_wait()`: [src/kernel/task.c:183](src/kernel/task.c#L183) polls `task_sleep_ms(1)` until the child exits.

**Fix:** use wait queues or event flags. `task_wait` should block on the child's exit and be woken by `task_exit`. The desktop and audio loops can block until input or a DMA half-complete event arrives.

### P7. Software EEVDF with 128-bit arithmetic on every tick (Medium)

The scheduler is EEVDF with a software 128-bit "wide" type ([src/kernel/sched_wide.c](src/kernel/sched_wide.c)). Each pick and tick runs `snapshot`, `mean`, `participants`, `executable` and `candidate` ([src/kernel/sched_policy.c](src/kernel/sched_policy.c)), and most of these are linear scans over `MAX_TASKS` (32, see [src/kernel/task.h:7](src/kernel/task.h#L7)). `scheduler_get_info()` also scans all slots.

**Impact:** with 100 Hz ticks and a 32-slot table this is probably acceptable, but it is an unbounded cost in the IRQ path and hard to audit. The 128-bit helpers are only needed to avoid overflow in the lag and virtual-time math.

**Fix:** measure the tick cost first (`startup_trace` can help). If it matters, keep the runnable set in a sorted structure and avoid the wide type where a bounded 64-bit range is provably safe.

### P8. Filesystem: no block or inode cache, one shared buffer (High)

- `fs.c` has a single static `io[BLOCK_SIZE]` buffer ([src/kernel/fs.c:52](src/kernel/fs.c#L52)), shared by every reader and writer under `filesystem_mutex`.
- `read_block()` ([src/kernel/fs.c:143](src/kernel/fs.c#L143)) goes to the disk on every call. There is no cache.
- Path resolution rereads each directory block and walks the tree for every open, stat and list.
- `fs_read_file_at()` ([src/kernel/fs.c:949-968](src/kernel/fs.c#L949-L968)) reads one 4 KB block per iteration and copies through a byte loop.
- `fs_write_file()` zeroes the shared buffer and copies byte by byte for each block ([src/kernel/fs.c:~1000](src/kernel/fs.c#L1000)).

**Fix:** add a small LRU block cache with write-through. Consider a directory-entry cache for hot path components. Read multi-block spans in one request when the blocks are contiguous.

### P9. Byte-at-a-time `memcpy` and `memset` (High)

The freestanding `memcpy` and `memset` are byte loops ([src/kernel/memory.c:3-12](src/kernel/memory.c#L3-L12)). Copies of pixel data, file blocks, network buffers and inode data all go through them. The same pattern is copied six times in the tree (`src/apps/doom/compat/libc.c`, `src/apps/syndig/compat/runtime.c`, `src/apps/tunnel/main.c`, `src/system/libraries/tls/runtime.c`, `src/system/user/runtime.c`, and the kernel).

**Fix:** implement `memcpy`/`memset` with `rep movsl`/`rep stosl` and a byte tail, as `video_write_span` already does. Keep a single implementation and link the rest against it.

### P10. Per-byte 24 bpp span writes (Low)

[src/kernel/hal/video.c:50-57](src/kernel/hal/video.c#L50-L57) writes 24 bpp framebuffers one byte at a time with `volatile`. This is only the non-32-bit path, but it is the slow case on older VESA modes.

**Fix:** convert each row to packed 24 bit in a scratch buffer and copy it with `rep movsb`, or use a dedicated routine.

### P11. Full-frame scans under the kernel mutex (Medium)

- `paging_memory_info()` ([src/kernel/paging.c:116-126](src/kernel/paging.c#L116-L126)) scans all `FRAME_COUNT` (about one million) frames bit by bit under `kernel_mutex`. Any memory-info query stalls the compositor.
- `paging_frame_alloc()` ([src/kernel/paging.c:201](src/kernel/paging.c#L201)) scans from `next_frame` bit by bit, then zeroes the page word by word. The contiguous path uses `rep stosl`, so this is inconsistent.

**Fix:** keep a free-frame counter, updated in `frame_set_used`, so `paging_memory_info` is O(1). Use `rep stosl` for the zeroing.

### P12. Allocation-heavy static tables (Low)

- `etherframe.c` allocates `handlers[65536]` ([src/kernel/net/etherframe.c:8](src/kernel/net/etherframe.c#L8)), about 256 KB of BSS for a table of 16-bit EtherTypes that only a few entries use.
- Kernel-stack locals: the window draw path holds a `user_3d_triangle_t triangles[300]` (about 8 KB) on a 32 KB task stack ([src/kernel/syscall.c:789](src/kernel/syscall.c#L789), [src/system/user/window_api.h:13-17](src/system/user/window_api.h#L13-L17)).

**Fix:** use a sparse table or a small hash for EtherTypes. Keep the triangle buffer in a per-task scratch area so deep call stacks cannot push it over the limit.

### P13. Build: every object depends on the Makefile (Low)

[Makefile:210-213](Makefile#L210-L213) sets `%.o: %.c Makefile` and `%.o: %.asm Makefile`. Any Makefile edit, including adding a single source file, rebuilds the entire kernel and every app. The Makefile also hand-lists kernel sources at lines 128-156 and reuses `/tmp/` for test binaries (lines 520-609).

**Fix:** depend on a generated include file or `.config` stamp, not the whole Makefile. Let the kernel source list come from `$(wildcard …)` with an explicit exclusion list. Put test binaries under `$(BUILD_DIR)`.

---

## 2. Architectural issues

### A1. Kernel depends on system and app code (High)

The kernel includes upper-layer code 38 times ([src/kernel/main.c:1-56](src/kernel/main.c#L1-L56) includes `system/graphics/wm`, `system/shell`, `apps/notepad`, `apps/viewer`). Examples include [src/kernel/idt.c:8](src/kernel/idt.c#L8) (crash dialog), [src/kernel/login.c:12](src/kernel/login.c#L12) (window manager), [src/kernel/drivers/power.c](src/kernel/drivers/power.c), and [src/kernel/net/ipv4.c](src/kernel/net/ipv4.c).

The dependency also goes the other way: `src/system` includes kernel headers in 57 files.

**Impact:** the layers cannot be built or tested separately. `src/kernel/main.c` (539 lines) is both the kernel entry point and the owner of the window manager's API table, and it grows with every feature.

**Fix:** define the direction explicitly: `kernel` → `hal` → `drivers` at the bottom, `system` (services, WM, shell) above, `apps` on top. Kernel code should expose hooks (callbacks registered at boot) rather than calling `wm_*` directly. Move the `kernel_api` table out of `main.c` into a `system/` bootstrap file.

### A2. Two-way service table via `os_api_t` (Medium)

`os_api_t` ([src/kernel/os_api.h](src/kernel/os_api.h)) is a large struct of function pointers that the kernel fills in ([src/kernel/main.c:58-120](src/kernel/main.c#L58-L120)) and the WM and desktop code call through `wm_api` ([src/system/graphics/wm/wm.c:30](src/system/graphics/wm/wm.c#L30)). Some callers use the table, others call the kernel directly, so both mechanisms coexist.

**Impact:** there is no single rule for how system code reaches the kernel. Every new feature adds a field to a struct that every module must agree on, and mismatches compile without warning.

**Fix:** pick one mechanism. Either a versioned table (with a size and version field) used by every upper layer, or direct calls through a narrow header per subsystem. Remove the mix.

### A3. Global mutable state and a single global lock (High)

- `kernel_mutex` ([src/kernel/mutex.c:2](src/kernel/mutex.c#L2)) protects framebuffer, window manager, paging, syscalls, cursor and settings. The same lock is taken by the renderer for an entire frame, by every pixel write, and by `paging_frame_alloc`.
- The window manager keeps state in file-level and function-local statics (for example `wm_api`, `redraw_pending`, and the `last_min`/`next_frame` timers in `wm_poll`).
- `mutex.c` says it is a single-CPU design: "An SMP port must replace this".

**Impact:** any slow operation under `kernel_mutex` delays input, rendering and every app. Changes to shared state are hard to reason about because there is no owner.

**Fix:** split the lock by subsystem (framebuffer, paging, WM, fs). Keep the WM state in one `struct wm_state` owned by the compositor task, and send it requests through a queue.

### A4. Layering of drivers and hardware abstraction (Medium)

- The HAL is partial. `hal/audio`, `hal/net`, `hal/usb`, `hal/video`, `hal/wifi` exist, but many callers still go to the driver directly.
- `hal/net.c` includes the AMD PCnet driver directly ([src/kernel/hal/net.c:5](src/kernel/hal/net.c#L5)), so the HAL is not a neutral layer.
- Audio has three paths: `sb16_driver`, `hda`, and `pc_speaker`. The choice is made in `hal/audio.c` with a static `sb16_ops` table, and the others are only partly wired.

**Fix:** make every driver register with its HAL class at init (`hal_net_register(&ops)`). Hide device-specific headers from the HAL's callers.

### A5. Duplicate and dead drivers (Medium)

- `src/kernel/net/amd/pcnet32.c` and `pcnet.c` both implement PCnet. `pcnet32` is not referenced anywhere (no build entry, no include), so it is dead code. It also defines `struct pcnet_init_block` at file scope.
- Two display paths for Intel: `drivers/display/intel_gma.c` and `drivers/display/i915/`, both built by `Makefile:143`.
- Display: `qemu.c`, `virtio_gpu.c`, `virgl.c`, `vga.c` and `i915` are all compiled in, and selection happens at runtime through `video_drivers.h`.

**Fix:** delete `pcnet32.c` after confirming nothing needs it. Decide whether `intel_gma` or `i915` is the supported path and remove the other.

### A6. Inconsistent error and synchronisation conventions (Low)

- Some APIs return `-1`, others `0`, others `uint8_t` booleans, and `task_wait` returns `0` both for success and for "already gone" ([src/kernel/task.c:183-197](src/kernel/task.c#L183-L197)).
- `mutex_lock` returns early when `!current_task` ([src/kernel/mutex.c:20](src/kernel/mutex.c#L20)), which silently skips locking during early boot. Callers cannot tell this apart from success.
- Some helpers use `MUTEX_GUARD`, others use `task_irq_save`, and some do both.

**Fix:** adopt a single return convention per module and document it in the header. Make the early-boot case explicit, for example with a `mutex_boot_mode` flag.

### A7. Naming and file organisation (Low)

- Three scheduler files (`scheduler.c`, `sched_policy.c`, `sched_wide.c`) plus `sched_policy.h` and `sched_wide.h` form one subsystem with no index.
- `src/kernel/net/wireless/intel/iwlegacy/tests/` and `src/kernel/net/wireless/broadcom/b43/tests/` sit next to driver code and are compiled from the Makefile with `/tmp` outputs.
- Tests are split between per-module `tests/` folders and a top-level `src/kernel/tests/`, so there is no single convention.
- `kernel/types.h` defines `SCREEN_WIDTH` as a variable in one configuration and a macro in another ([src/kernel/types.h:19-23](src/kernel/types.h#L19-L23)). Code that compares the two cannot be verified without knowing which build is active.

**Fix:** standardise test layout (`tests/` beside each module, or one top-level `tests/`). Make the screen size a runtime value everywhere, or a compile-time constant everywhere.

### A8. Documentation overlap (Low)

- Several `documentation/*.md` files cover the scheduler (`eevdf-arithmetic.md`, `eevdf-policy.md`, `scheduling.md`, `linux-multitasking.md`). Their overlap was not checked line by line; a read-through is needed before merging or cross-linking them.

---

## 3. Recommended order

1. **P9 and P1**: `memcpy`/`memset` and per-page user validation. Small change, broad effect, low risk.
2. **P2 and P4**: remove the per-pixel mutex and cache the wallpaper. Visible frame-rate gain.
3. **P3**: dirty-rectangle compositing. The largest rendering improvement, but a larger change.
4. **P8**: block cache in the filesystem.
5. **P5 and P6**: interrupt-driven NICs and event-based wakeups. Needs hardware testing.
6. **A1 and A3**: layering and locking. Long-term, structural; do after the performance fixes so the baseline is measured.
7. **A5 and A7**: remove dead and duplicate code. Quick wins that reduce what must be maintained.

Before starting, measure: `STARTUP_TRACE=1` and the existing `render` timing counters give a baseline for P1–P4 and P6.

---

## 4. Notes on what was checked and found acceptable

- **PS/2 port I/O** is wrapped in `startup_trace_now()`, but that macro compiles to `0ull` unless `STARTUP_TRACE=1`, so there is no cost in normal builds.
- **Kernel compilation** uses `-O2 -m32 -ffreestanding`, which is appropriate.
- **Scheduler lock handling** uses priority inheritance on `kernel_mutex`. The design is sound for a single CPU.
- **Syscall bounds checks** are present on every user pointer. The concern is the cost (P1), not correctness.
