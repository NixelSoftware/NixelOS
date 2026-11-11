# On-device C SDK

Build the tools with `make sdk`, or build a complete image with `make`.
The image includes `ncc` (a small native i386 C compiler), `designer` (the
window designer), and the runtime at `/usr/lib/nixel/sdkhost.bin`. No compiler
server, network connection, assembler, or linker is required on the device.

## Try an application

The bundled example is `/usr/share/nixel/sdk/hello.c`. In a terminal:

```
ncc /usr/share/nixel/sdk/hello.c hello.bin
./hello.bin
```

Launch `designer` to create a window, or
`designer /home/user/Documents/MainWindow.c` to choose its source path.
The initial source path is `MainWindow.c` in the inherited working directory.
Choose **Open** to load an existing designer source file. Source files in
`/usr/share` are read-only; save your own files in your home directory.

The designer fits the default 800×600 desktop. Add buttons, text boxes,
labels, or checkboxes from the palette. Click a component to select it;
change its text, X/Y position, width/height, or numeric ID, then click **Apply**.
Click empty canvas to move the selected component there. **Delete** removes
it. The window title and dimensions are also edited with **Apply**.
The designer supports windows 160–600 pixels wide and 100–300 pixels high.

**Code** switches to the C source editor. **Save** writes C source. **Build** saves and compiles to the same path with
`.c` replaced by `.bin` (or `.bin` appended). **Run** builds and launches that
executable. Status and compile errors appear along the bottom. Builds replace
an existing output file. Save refuses an existing source file without the
designer markers, and preserves all C code outside those markers.

## Window object and event handling

C has no classes. The runtime owns a `sdk_window` struct containing the native
window handle, controls, focus, and checkbox state. The designer visualizes
the `MainWindow()` initializer that constructs this object. This first version
supports one main window per SDK application and up to 48 components.

```c
#include <nixel.h>

void MainWindow(void) {
    window("My app", 400, 240);
    textbox(1, 16, 40, 260, 28, "Your name");
    button(2, 16, 84, 120, 28, "Greet");
    label(3, 16, 132, 350, 24, "");
}

int main(void) {
    MainWindow();
    int event = 0;
    while (event >= 0) {
        event = poll();
        if (event == 2) {
            set_text(3, get_text(1));
        }
    }
    return 0;
}
```

For designer round trips, use a file created by the designer or the bundled
example, which surrounds `MainWindow()` with `NIXEL DESIGNER BEGIN/END`
comments. Click **Code** to edit your event handlers outside that generated section
in the built-in C editor. It supports arrow keys, Home/End, Backspace, Delete,
newlines, Tab indentation, and automatic scrolling. Use **Save**, **Build**,
and **Run** while editing. Click **Design** to save and visualize your window
again. You can open handwritten C source in Code mode as well; returning
to Design requires a valid designer section. Manually changing the generated section must retain
the generated literal-call syntax for the designer to reopen it.

Component functions take `(id, x, y, width, height, text)`. IDs must be unique
positive integers. Coordinates are relative to the outer native window;
Y must be at least 26 to clear its title bar. Text is copied into a 128-byte
buffer (127 characters plus terminator). Text boxes support printable ASCII,
backspace, and Tab navigation. Labels and input text are clipped to their
available columns. Button clicks and checkbox toggles return their ID from
`poll()`. It returns 0 while idle, and -1 when the main window closes; idle
polls yield to the scheduler. `checked(id)` reads checkbox state.
`get_text(id)` returns a borrowed pointer; `set_text(id, text)` copies text
and redraws. `close_window()`, `print(text)`, `ticks()`, and `yield()` are also
available. Function declarations are in `/usr/share/nixel/sdk/nixel.h`.

## Compiler scope and limits

NCC is an initial C subset compiler, not a complete ISO C implementation.
It emits native i386 machine code into a copy of the bundled runtime. Its
`.bin` output is a standalone ring-3 application with the normal APP1 header;
the compiler and original source are not needed to run it.

Supported syntax:

- `int`, `char *` string handles, `void`, integer and character literals,
  escaped strings, and `//` / `/* */` comments.
- Functions with named parameters, forward calls and recursion; `int main(void)`
  is the entry point. Functions accept at most eight word-sized arguments.
- Global integer variables with constant integer initializers, local variables,
  assignment, blocks, `if` / `else`, `while`, and `return`.
- `+ - * / %`, comparisons, unary `+ - !`, and short-circuit `&& ||`.
- Exactly `#include <nixel.h>` or `#include "nixel.h"`; the SDK functions are
  built into the compiler's runtime symbol table.

Values and parameters occupy 32 bits. Strings are opaque pointers for SDK
calls. This version does not support structs in application source, arrays,
pointer arithmetic/dereferencing, floating point, casts, typedefs, arbitrary
headers/macros, prototypes, `for`, `break`, `switch`, `++`, compound assignment,
or external libraries. Use `i = i + 1` and `while` loops. Native OS apps can
continue using the repository's host GCC toolchain and full syscall API.
The window struct API is also available internally in `src/sdk/ui.h` for
host-built native apps.

Source size is limited to 16,383 bytes. Generated code, globals, and strings
share a 24 KiB reservation; identifiers, symbol counts, local variables,
call fixups, and parse nesting are bounded. Errors abort the build before
writing an executable. Runtime arithmetic errors such as division by zero
remain application faults. Application output permissions are set to 0755
through an owner-checked `SYS_FS_CHMOD` call.

## Verification

`make test-sdk` builds all three tools, executes generated i386 machine code
in a freestanding Linux test runner, checks compiler rejection paths, compiles
the example, tests designer serialization and preservation of handwritten
code, and exercises the production UI against mocked window syscalls.
The native execution portion requires Linux with 32-bit syscall support;
it may need permission to run outside a restrictive execution sandbox.
