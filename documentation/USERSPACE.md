# Userspace transition

The kernel now has a first i386 ring-3 path. The existing application loader
still executes legacy applications in ring 0 and passes them `os_api_t`; only
apps marked as user apps enter ring 3. Remaining legacy GUI and console apps
use the ring-0 `os_api_t` callback interface until they are migrated.

The first system-call ABI lives in `src/system/user/sys_api.h`. Calls use
`int 0x80`, with the call number in EAX, arguments in EBX/ECX/EDX, and the
result in EAX:

User executables are linked and loaded at `0x00C00000` and receive a private
mapping through `0x00E00000`. This keeps their writable image and data away
from the kernel's global data and display buffers.

| Number | Call | Arguments | Result |
| --- | --- | --- | --- |
| 1 | `sys_write` | EBX = buffer, ECX = byte count (up to 256) | bytes written, or -1 |
| 2 | `sys_get_ticks` | none | current timer tick count |
| 3 | `sys_exit` | EBX = process status | returns to the kernel app loader |
| 4 | `sys_shutdown` | none | requests kernel-managed system shutdown |
| 5 | `sys_net_diagnostics` | EBX = writable `net_diagnostics_t*` | snapshot status in EAX (`1` ready, `0` not ready, `-1` invalid buffer) |
| 6 | `sys_dns_resolve` | EBX = hostname, ECX = writable IPv4 output | `1` resolved, `0` lookup failed, `-1` invalid pointer |
| 7 | `sys_ping_start` | EBX = destination IPv4, ECX = sequence | request handle, or `-1` if it could not be submitted |
| 8 | `sys_ping_poll` | EBX = request handle, ECX = writable milliseconds output | `0` pending, `1` reply received, `2` timed out, `-1` invalid handle or pointer |
| 9 | `sys_yield` | none | services kernel network and window/input polling before returning |
| 10 | `sys_window_create` | EBX = readable `user_window_desc_t*` | window handle, or `-1` |
| 11 | `sys_window_draw` | EBX = handle, ECX = readable `user_window_draw_t*` | `0` success, `-1` invalid request |
| 12 | `sys_window_poll` | EBX = handle, ECX = writable `user_window_event_t*` | `1` event, `0` no event, `-1` invalid handle or pointer |
| 13 | `sys_window_present` | EBX = handle | `0` success, `-1` invalid handle |
| 14 | `sys_window_close` | EBX = handle | `0` success, `-1` invalid handle |
| 15 | `sys_fs_read` | EBX = filename, ECX = writable buffer, EDX = capacity (up to 16 KiB; zero queries size) | bytes read, size, or `-1` |
| 16 | `sys_fs_list` | EBX = writable `FS_MAX_FILES` by `FS_MAX_FILENAME` filename array | file count, or `-1` |
| 17 | `sys_floppy_init` | none | `1` ready, `0` unavailable |
| 18 | `sys_floppy_read` | EBX = LBA, ECX = sector count (1–18), EDX = writable output | `1` success, `0` failed or invalid request |
| 19 | `sys_floppy_write_test_pattern` | none | writes the diagnostic pattern to reserved floppy LBA 2879; `1` success, `0` failure |
| 20 | `sys_fs_write` | EBX = filename, ECX = readable buffer, EDX = length (up to 64 KiB) | filesystem write result |
| 21 | `sys_tcp_connect` | EBX = remote IPv4, ECX = port | socket handle, or `-1` |
| 22 | `sys_tcp_state` | EBX = socket handle | TCP state, or `-1` |
| 23 | `sys_tcp_send` | EBX = handle, ECX = readable data, EDX = length (up to 1448) | `1` sent, `0` failed |
| 24 | `sys_tcp_read` | EBX = handle, ECX = writable output, EDX = capacity (up to 4096) | bytes received, or `-1` |
| 25 | `sys_tcp_close` | EBX = socket handle | `0` closed, or `-1` |
| 26 | `sys_dns_start` | EBX = hostname | resolver handle, or `-1` if another lookup is active |
| 27 | `sys_dns_poll` | EBX = resolver handle, ECX = writable IPv4 output | `0` pending, `1` resolved, `2` failed |
| 28 | `sys_memory_info` | EBX = writable `user_memory_info_t*` | `0` success, `-1` invalid output buffer |

`user_memory_info_t` is defined in `src/system/user/memory_api.h` as four
32-bit byte counts: `total_bytes`, `used_bytes`, `available_bytes`, and
`reserved_bytes`, in that order (16 bytes total). The syscall validates the
entire output range as writable user memory. The snapshot includes the calling
app's pages, page tables, and allocated window surfaces. Available RAM is the
free page-frame pool; used RAM includes allocated frames plus reserved memory,
so `total_bytes == used_bytes + available_bytes`.

These numbers describe the current configured 32 MiB layout, not detected
physical RAM. The lower 16 MiB is reserved for the kernel, fixed buffers,
stacks, firmware regions, and bootstrap mappings. The upper 16 MiB is managed
by the frame allocator. Changing QEMU RAM alone does not change this layout;
BIOS memory-map discovery remains future work.

`sys_write` accepts only readable user pages in the current app address space.
`sys_shutdown` is a privileged service exposed through the syscall dispatcher;
user apps do not call kernel power routines directly. `shutdown` is the second
app using the ring-3 path. `netdiag` is the third ring-3 app and demonstrates
the diagnostics snapshot syscall. `sys_write` forwards output to the Terminal
window. User apps receive their command
arguments as the first `user_main` parameter. `crash_debug` is the fourth ring-3
app; it deliberately faults outside its mapped address range to demonstrate
that a user page fault terminates the app and returns control to the shell.
`ping` is the fifth ring-3 app and uses kernel-mediated DNS and asynchronous
ICMP request handles. Requests are owned by the calling address space and can
be polled independently. While waiting, ping calls `sys_yield` so the kernel
continues network and window/input polling. The runner still executes one user
app at a time, so this cooperative yield keeps the system event loop responsive
but does not provide simultaneous user processes; that needs scheduler/process
work to follow.
`calc` is the sixth ring-3 app and retains its calculator window. Ring-3 window
apps create kernel-managed frames, draw into a private window surface through
validated fill, text, button, and PNG image syscalls, and receive queued key,
mouse, redraw, and close events. The WM composites those surfaces in window
z-order. Apps call
`sys_yield` to let the window manager process input, then poll their event queue.
The window manager never invokes a user callback while running in ring 0.
`mines` is the seventh ring-3 app and uses the same window and event interface
for its Minesweeper board.
`hexview` is the eighth ring-3 app; it reads files through the bounded
`sys_fs_read` call and draws its viewer through the window API.
`files` is the ninth ring-3 app and uses `sys_fs_list` plus zero-capacity
`sys_fs_read` size queries to populate its file list.
`floppy_debug` is the tenth ring-3 app. Floppy reads are bounded to the 1.44 MB
media range, and the write diagnostic is limited to the reserved final sector.
`http_test` is the eleventh ring-3 app. TCP sockets belong to their creating
address space, queue received bytes for polling, and are closed when the app exits.
`curl` is the twelfth ring-3 app and uses the same socket API for HTTP requests,
printing responses or saving response bodies through `sys_fs_write`. It uses
`sys_dns_start` and `sys_dns_poll` to keep DNS waits cooperative.
`browser` is the thirteenth ring-3 app. It creates its own WM surface, receives
keyboard and mouse events through the user window API, and fetches HTTP pages
with the owner-scoped DNS and TCP syscalls. It opens the original
`home.menifeetech.pp.ua` homepage at startup and retains the original HTML
layout, links, form controls, and PNG image handling; it supports HTTP on port
80. PNGs are decoded into the app-owned window surface by the WM.
The GDT includes ring-3 code/data segments and a TSS kernel stack, while IDT
vector `0x80` is callable from ring 3. `userhello` is the first ring-3 app;
launch it from the shell to exercise write, tick, and exit calls.

The first runner is synchronous and supports one user app at a time. Timer
interrupts return to that app, but this is not yet preemptive process
scheduling. Existing GUI and console apps continue to use the legacy ring-0
`os_api_t` callback interface. Moving them requires replacing those direct
callbacks with kernel-mediated services.
