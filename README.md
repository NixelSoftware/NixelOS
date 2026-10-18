pmo

# Terminal architecture

The terminal emulator is implemented as the application in
`src/apps/terminal/terminal.c`. It owns the terminal window, input editing,
scrollback, prompt, and rendering. Shell commands continue to execute in the
kernel, where they retain access to the filesystem, networking, and built-in
applications.

The terminal app submits a command through `os_api_t.shell_execute`. Kernel
output is delivered back through callbacks, so the terminal behaves as a
separate emulator instead of embedding shell parsing and terminal rendering in
the kernel window manager.

Application binaries begin with an executable header identifying them as GUI
or console applications. The kernel launches GUI applications with terminal
output disconnected, while console applications inherit the terminal output
callback and receive their command arguments through `os_api_t`.

`ping` and `netdiag` are console applications. Their networking operations are
performed through kernel-provided network services, but their argument parsing
and output formatting run in the applications and appear in the terminal.
# Building
compile on windows with build.bat
