# Userspace transition

The kernel is preparing for i386 ring 3. The existing application loader still
executes legacy applications in ring 0 and passes them `os_api_t`; those
applications have not crossed the privilege boundary yet.

The first system-call ABI lives in `src/system/user/sys_api.h`. Calls use
`int 0x80`, with the call number in EAX, arguments in EBX/ECX/EDX, and the
result in EAX:

| Number | Call | Arguments | Result |
| --- | --- | --- | --- |
| 1 | `sys_write` | EBX = buffer, ECX = byte count (up to 256) | bytes written, or -1 |
| 2 | `sys_get_ticks` | none | current timer tick count |

`sys_write` accepts only readable user pages in the current app address space.
The GDT includes ring-3 code/data segments and a TSS kernel stack, while IDT
vector `0x80` is callable from ring 3. The next step is context-aware
scheduling and process termination; only then should the loader enter ring 3.
After that, services can move from direct `os_api_t` callbacks to syscalls in
small groups.
