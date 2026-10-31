# Writing an App

Applications export `user_main(const char* arguments)` and use
`src/system/user/sys_api.h`. Every loaded executable runs in ring 3, including
applications started by root or sudo. Kernel callbacks in `os_api_t` are an
internal interface; an application does not receive them.

```c
#include "../../system/user/sys_api.h"

void user_main(const char* arguments) {
    (void)arguments;
    static const char message[] = "Hello from my app!\n";
    sys_write(message, sizeof(message) - 1);
}
```

Copy an existing app's `entry.asm` (for example `src/apps/userhello/entry.asm`).
The eight-byte executable header must contain `APP_HEADER_MAGIC` followed by
`APP_TYPE_USER`:

```asm
dd 0x31505041
db 3, 0, 0, 0
```

The entry stub calls `user_main` with the argument string, then issues
`SYS_EXIT`. Headers 1 and 2 are rejected even for root; an executable cannot
request kernel execution. Link at `0x00C00000` using the existing Makefile app
rules. The loader provides the private app mapping and zeroed BSS space.

## Permissions

The kernel checks the calling task's UID before performing sensitive ABI calls.
UID 0 is privileged. Membership in group 0 and the `system` account (UID 1) do
not grant these privileges. There is no executable-header flag or syscall
argument that grants elevation.

| Service | Required privilege |
| --- | --- |
| `sys_shutdown`, `sys_reboot` | UID 0 |
| `sys_floppy_init`, `sys_floppy_read`, `sys_floppy_write_test_pattern` | UID 0; raw media bypass filesystem permissions |
| `sys_serial_log_read` | UID 0; reads shared kernel logs |
| `sys_tcp_listen` on ports 1–1023 | UID 0 |
| `sys_tcp_listen` on ports 1024–65535 | Any user |
| Filesystem reads/writes/listing/mkdir/delete | Normal filesystem ownership, directory and mode checks |
| Windows, mixed audio, outgoing TCP, DNS, ping, read-only diagnostic snapshots | Any user, with existing buffer and resource ownership checks |

Unauthorized protected calls return `SYS_ERR_PERMISSION` (-2), without touching
hardware, user buffers or allocating service resources. Other errors keep their
existing ABI return values. Authorized callers still undergo normal parameter,
buffer and ownership validation.

Use `sudo shutdown`, `sudo reboot`, `sudo floppy_debug`, `sudo com0`, or
`sudo qkftp` in the terminal; sudo authenticates with the root password.
The system menu's shutdown dialog is a built-in window-manager component,
linked into the kernel. It can shut down or restart without a sudo prompt.
It loads no executable from disk and does not elevate the desktop session or
any application. App power syscalls still require UID 0.

`sudo -i` starts a root shell. Child apps inherit the launching task's UID and
GID, including detached children. Returning the shell to its ordinary UID does
not revoke an already launched root child's privileges. There is no setuid
executable support.

Power wrappers now return a signed result on failure. Always handle it:

```c
int32_t result = sys_shutdown();
if (result == SYS_ERR_PERMISSION) {
    static const char denied[] = "Run sudo shutdown.\n";
    sys_write(denied, sizeof(denied) - 1);
}
```

Raw floppy calls return 1 on success. Compare against 1 explicitly; a negative
permission error is not a successful boolean result. `sys_fs_delete(path)`
removes a regular file and returns 0 on success or -1 on failure.

## Windows and events

Create a window with `sys_window_create`, draw using `sys_window_draw`, then
submit it with `sys_window_present`. Receive input, redraw and close events
through `sys_window_poll`; call `sys_yield` while waiting. Applications own only
their window surfaces and never install kernel callbacks. Drawing and mouse
event coordinates are local to the window. See `src/apps/calc` or
`src/apps/oobe` for examples.

The window manager composites surfaces independently. Drawing operations
include fills, text, buttons, PNG images, packed pixels, and bounded triangle
batches. See `window_api.h` for operations and bounds.

## Files, networking and audio

File paths resolve relative to the inherited working directory. Use
`sys_fs_home` for the caller's home. Check every result; file reads do not add
a string terminator. `sys_fs_read_at` supports bounded offset reads.

TCP handles, DNS requests, windows and audio streams are scoped to the creating
address space. They are released when the app exits. Prefer asynchronous DNS
and yield while polling sockets or waiting for input. Network services on
privileged ports must be launched through sudo or a root shell.

PCM audio uses a caller-owned stream through the shared kernel mixer. Handle
partial writes and queue backpressure; drain before shutdown when the final
samples must finish. See [USERSPACE.md](USERSPACE.md) for ABI and audio details.
