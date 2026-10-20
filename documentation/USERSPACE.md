# Userspace transition

The kernel now has a first i386 ring-3 path. The existing application loader
still executes legacy applications in ring 0 and passes them `os_api_t`; only
apps marked as user apps enter ring 3.

The first system-call ABI lives in `src/system/user/sys_api.h`. Calls use
`int 0x80`, with the call number in EAX, arguments in EBX/ECX/EDX, and the
result in EAX:

| Number | Call | Arguments | Result |
| --- | --- | --- | --- |
| 1 | `sys_write` | EBX = buffer, ECX = byte count (up to 256) | bytes written, or -1 |
| 2 | `sys_get_ticks` | none | current timer tick count |
| 3 | `sys_exit` | EBX = process status | returns to the kernel app loader |
| 4 | `sys_shutdown` | none | requests kernel-managed system shutdown |
| 5 | `sys_net_diagnostics` | EBX = writable `net_diagnostics_t*` | snapshot status in EAX (`1` ready, `0` not ready, `-1` invalid buffer) |

`sys_write` accepts only readable user pages in the current app address space.
`sys_shutdown` is a privileged service exposed through the syscall dispatcher;
user apps do not call kernel power routines directly. `shutdown` is the second
app using the ring-3 path. `netdiag` is the third ring-3 app and demonstrates
the diagnostics snapshot syscall. `sys_write` forwards output to the active
shell output and the debug serial stream.
The GDT includes ring-3 code/data segments and a TSS kernel stack, while IDT
vector `0x80` is callable from ring 3. `userhello` is the first ring-3 app;
launch it from the shell to exercise write, tick, and exit calls.

The first runner is synchronous and supports one user app at a time. Timer
interrupts return to that app, but this is not yet preemptive process
scheduling. Existing GUI and console apps continue to use the legacy ring-0
`os_api_t` callback interface. Moving them requires replacing those direct
callbacks with kernel-mediated services.
