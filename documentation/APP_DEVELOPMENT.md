# Writing an App

This page describes the current C app interface. The kernel passes an `os_api_t*` to an app's `main_app` function. Include the public headers from `src/kernel` and use that table for system services.

```c
#include "../../kernel/os_api.h"

int main_app(os_api_t* os) {
    if (!os) return -1;
    os->term_print("Hello from my app!\n");
    return 0;
}
```

Each executable starts with the app header expected by the loader. The first eight bytes are the magic value and app type; the assembly entry point then calls `main_app(os_api)`. Existing apps have an `entry.asm` showing the format:

```asm
dd 0x31505041 ; APP_HEADER_MAGIC
db 1, 0, 0, 0 ; APP_TYPE_GUI (use 2 for APP_TYPE_CONSOLE)
```

Keep the header and entry stub consistent with the app type and the build setup. See `src/apps/calc` for a GUI example and `src/apps/ping` or `src/apps/curl` for console examples.

## Console apps

A console app writes output with `term_print`. The shell supplies command arguments through `command_args`; parse that string in the app. `main_app` returns an integer status, though the current shell loader does not present a rich process or error model.

```c
#include "../../kernel/os_api.h"

int main_app(os_api_t* os) {
    if (!os) return -1;

    const char* args = os->command_args();
    if (!args || !args[0]) {
        os->term_print("Usage: greet <name>\n");
        return 1;
    }

    os->term_print("Hello, ");
    os->term_print(args);
    os->term_print("\n");
    return 0;
}
```

For commands that need to print while using shell services, `shell_execute` accepts an output callback and a clear-screen callback. The terminal app demonstrates how to use it. `term_print` writes to the active shell output; it is not a formatted `printf`, so build strings yourself when you need numbers or formatting.

## GUI apps

GUI apps create a `Window`, provide drawing and input callbacks, then register it. Use static storage for the window and its displayed data so they remain valid after `main_app` returns. Window coordinates and drawing API coordinates are screen coordinates; mouse callback coordinates are local to the window.

```c
#include "../../kernel/os_api.h"
#include "../../kernel/window.h"

static os_api_t* api;
static Window window;
static int clicks;

static void draw(Window* win) {
    api->fill_rect(win->x + 4, win->y + 24, win->w - 8, win->h - 28,
                   0x00FFFFFF);
    api->draw_string(win->x + 12, win->y + 36, "Click anywhere", 0x00000000);
}

static void key(Window* win, char c) {
    if ((uint8_t)c == 27 || (uint8_t)c == 0x01)
        api->unregister_window(win);
}

static void mouse(Window* win, int32_t x, int32_t y, uint8_t button) {
    (void)x; (void)y;
    if (button == 1) {
        clicks++;
        draw(win);
        api->blit_all();
    }
}

int main_app(os_api_t* os) {
    if (!os) return -1;
    api = os;
    window.x = 100; window.y = 80; window.w = 240; window.h = 140;
    window.title = "Example";
    window.visible = 1; window.focused = 1;
    window.draw_content = draw;
    window.handle_key = key;
    window.handle_mouse = mouse;
    if (api->register_window(&window) < 0) return -1;
    return 0;
}
```

Callbacks may be called after `main_app` returns, so do not register a pointer to a stack-local `Window` or callback data. Redraw after changing state, then call `blit_all` (or `blit_rect` for a changed region). The keyboard callback receives a character for printable input and may also receive special key codes; existing GUI apps show the codes used for navigation and close handling. Use `draw_button` for the system's standard button appearance and `handle_mouse` to detect clicks.

## System API (`os_api_t`)

The API is defined in `src/kernel/os_api.h`. The table currently provides these calls:

### Drawing and windows

| Call | Purpose |
| --- | --- |
| `fill_rect(x, y, w, h, color)` | Fill a screen rectangle. Colors use `0x00RRGGBB`. |
| `draw_string(x, y, text, color)` | Draw text at screen coordinates. |
| `draw_button(x, y, w, h, label, pressed)` | Draw a standard button. |
| `blit_rect(x, y, w, h)` | Copy a screen rectangle to the display. |
| `blit_all()` | Copy the full screen to the display. |
| `register_window(Window*)` | Register a GUI window; returns a status integer. |
| `unregister_window(Window*)` | Remove a registered window. |
| `show_info_dialog(title, message)` | Show the system information dialog. |

`Window` and its callback signatures are in `src/kernel/window.h`. A window has position and size, title, visibility/focus flags, a draw callback, and optional key and mouse callbacks.

### PNG images

| Call | Purpose |
| --- | --- |
| `png_decode_and_resize(data, len, pixels)` | Decode and resize a PNG to `PNG_TARGET_W` by `PNG_TARGET_H` pixels; returns success status. |
| `png_render(data, len, x, y, w, h)` | Decode and draw a PNG in the requested rectangle. |

The fixed output dimensions are declared in `src/kernel/png.h`.

### Filesystem

| Call | Purpose |
| --- | --- |
| `fs_list(output)` | Fill a `FS_MAX_FILES` by `FS_MAX_FILENAME` filename array; returns the number of entries/status. |
| `fs_read(name, dest, max_bytes)` | Read a file into the caller's buffer; returns bytes read or a negative error. |
| `fs_write(name, data, size_bytes)` | Write bytes to a file; returns a status/byte count. |
| `fs_delete(name)` | Delete a file; returns status. |

Filename and file-count limits are defined in `src/kernel/fs.h`. Check return values and reserve space for a trailing zero byte yourself when treating file data as a C string.

### Shell and time

| Call | Purpose |
| --- | --- |
| `term_print(text)` | Write a null-terminated string to shell output. |
| `command_args()` | Get the current command's argument string. |
| `shell_execute(command, output, clear)` | Run a shell command and route its output/clear operation through callbacks. |
| `get_ticks()` | Read the kernel frame/tick counter. |

### Networking

| Call | Purpose |
| --- | --- |
| `net_poll()` | Poll network processing. |
| `net_is_ready()` | Return whether the network is ready. |
| `net_get_diagnostics(out)` | Fill a `net_diagnostics_t` structure (declared in `src/kernel/drivers/net.h`). |
| `icmp_request_echo(ip, id, seq, payload, len)` | Send an ICMP echo request; IPv4 addresses are represented as a `uint32_t`. |
| `icmp_set_echo_reply_handler(callback)` | Set the ICMP echo reply callback. |
| `ipv4_send(ip, protocol, payload, len)` | Send an IPv4 payload for the protocol number. |
| `ipv4_set_handler(protocol, callback)` | Register a receive callback for an IPv4 protocol. |
| `ipv4_get_local_ip()` | Return the local IPv4 address as a `uint32_t`. |
| `dns_resolve(hostname, address_out)` | Resolve a hostname into an IPv4 address; returns success status. |

Callback and packet types are declared in `src/kernel/net/icmp.h`, `src/kernel/net/ipv4.h`, and `src/kernel/net/dns.h`. A protocol handler should validate packet lengths before reading fields. Network callbacks are invoked from network polling, so keep them short and avoid blocking there.

### Floppy and power

| Call | Purpose |
| --- | --- |
| `floppy_init()` | Initialize the floppy device. |
| `floppy_read(lba, count, buffer)` | Read sectors into the caller's buffer. |
| `floppy_write(lba, count, buffer)` | Write sectors from the caller's buffer. |
| `shutdown()` | Request system shutdown. |
| `reboot()` | Request system reboot. |

## Headers and building

At minimum, include `os_api.h`. GUI apps should also include `window.h`; filesystem and network structures are available through headers included by `os_api.h`, or can be included directly when their constants/types are needed. Build the project with `build.sh` on Linux or `build.bat` on Windows. Follow the existing app directory and build-script patterns when adding a new app.

## Unincluded apps

In the future, we will provide the developers an SDK to build apps without needing to compile the entire OS.