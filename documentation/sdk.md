# On-device C SDK

Create the window in `designer` and write its behavior in C event handlers.
The designer owns the layout. The Code editor shows only application code:
there are no drawing calls, control IDs, generated initialization, or event
loops to maintain.

Build the tools with `make sdk`, or include them in a full image with `make`.
Both the designer and the `ncc` compiler run on NixelOS without a network
connection or a host compiler.

## Projects and the designer

A project has two files next to each other:

- `MainWindow.c`: application functions and event handlers, edited in Code.
- `MainWindow.form`: designer-owned layout and component names. Edit it visually.

Open `designer` to start a project, or pass a source path such as
`designer /home/user/Documents/MainWindow.c` to open one. Choose **Open** after
changing the Source path. Save in a writable directory in your home.

Add buttons, text boxes, labels, and checkboxes using the palette. Select a
component to change its **Name**, text, position, and size, then choose
**Apply**. Names such as `Button1`, `SaveButton`, or `Username` must be unique
C identifiers, up to 31 characters. The window is publicly named `MainWindow`.
Click empty canvas to move a selected component, or choose **Delete** to
remove it. The designer supports 48 components and window sizes from
160×100 to 600×300 on the default 800×600 desktop.

**Code** opens the app's C file, including event-handler stubs for its named
components. **Design** returns to the visual layout and preserves your code.
The editor supports arrows, Home/End, Delete, Backspace, Tab indentation,
newlines, and scrolling.

**Save** writes both files. **Build** saves the project and compiles it to
`MainWindow.bin`; **Run** builds and launches that executable. The `.form`
file stays separate from app code and is embedded in the executable at build
time. Running the app does not need the `.c` or `.form` files.

Renaming a component publishes its new name and creates a new handler stub.
Your previous handler remains in the C file so its implementation is not lost;
move that logic into the new handler and update old component references.
Deleting a component also preserves handwritten code; remove any references
to the deleted object before building.

## Public components and event handlers

The public component types are `Window`, `Button`, `Checkbox`, `TextBox`, and
`Label`. In C these are component structs, and event handlers are ordinary
functions. The project compiler supplies the named instances to every
function in the application's C file. Do not redeclare those instances or
write your own `main()` for a designer project.

| Component | Public state | Automatic event handler |
| --- | --- | --- |
| `MainWindow` | `windowLoaded`, `isOpen` | `MainWindowOnLoaded()`, `MainWindowOnClosed()` |
| `Button1` | `buttonIsPressed`, `text` | `Button1OnPressed()` |
| `Checkbox1` | `checkboxIsTicked`, `isChanged`, `text` | `Checkbox1OnChanged()` |
| `TextBox1` | `text`, `textChanged` | `TextBox1OnTextChanged()` |
| `Label1` | `text` | None |

`Button1OnPressed()` is called once for each button activation. You can read
`Button1.buttonIsPressed` from another application function during that event.
It is an event pulse, cleared before the next event is dispatched, rather than
a measurement of how long the mouse button is held. `isPressed` is an alias.

`Checkbox1.checkboxIsTicked` reads or changes its checked state (`isTicked` is
an alias). `MainWindow.windowLoaded` becomes true after the native window has
been created (`isLoaded` is an alias), and its Loaded handler runs once after
all components are available. `isOpen` becomes false when it closes; the
Closed handler then runs once. Use `close_window()` to close it from app code.

Assign `.text` to change a button caption, label, input, or checkbox caption.
Read `TextBox1.text` to get the current input. Text is copied into a 128-byte
buffer, including its terminator; longer text is truncated. Changes to public
text and checkbox state are applied when the handler returns. Text-box edits
and programmatic text changes raise `OnTextChanged`; checkbox changes raise
`OnChanged`. Assigning the existing value does not raise a new change event.
Event flags and window state are read-only; the compiler rejects assignments
to them. Accessing a property on the wrong component type is also an error.

For a form containing `Button1`, `Checkbox1`, `TextBox1`, and `Label1`, write:

```c
#include <nixel.h>

void MainWindowOnLoaded(void) {
    Label1.text = "Ready";
}

void UpdateMessage(void) {
    if (Button1.buttonIsPressed) {
        Label1.text = TextBox1.text;
    }
}

void Button1OnPressed(void) {
    UpdateMessage();
}

void Checkbox1OnChanged(void) {
    if (Checkbox1.checkboxIsTicked) {
        Label1.text = "Enabled";
    } else {
        Label1.text = "Disabled";
    }
}
```

The layout creation and the calls to these handlers are supplied privately by
the project compiler. You may add application functions and globals and call
them from any handler. Handlers must have no parameters; use the generated
`void HandlerName(void)` signatures. Missing handlers are optional, and the
designer adds empty stubs without replacing existing implementations.

## Compile a project from the terminal

The bundled `hello.c` and `hello.form` demonstrate named public controls and
`Button1OnPressed()`. Compile and run them with:

```
ncc /usr/share/nixel/sdk/hello.c hello.bin
./hello.bin
```

To edit the example, copy **both** files into your own directory before
opening `hello.c` in the designer. The compiler automatically reads the
matching `.form` beside the C file and combines the layout and app logic in
memory. It never writes generated GUI code into the app's C file.

An invalid sidecar causes a build error. A standalone C file without a `.form`
can still use the original C subset compiler and its own `int main(void)`.
Older files that mix a `MainWindow()` drawing initializer into app code remain
standalone sources; create a new designer project to use the separated format.
The designer refuses to overwrite an existing file until you explicitly open
its project.

## Compiler scope and limits

NCC produces native i386 `.bin` apps with the normal APP1 header, window
syscalls, and process permissions. The runtime is bundled at
`/usr/lib/nixel/sdkhost.bin`.

Supported application syntax includes integer globals and locals, functions,
forward calls and recursion, `if`/`else`, `while`, `return`, assignments,
strings, arithmetic, comparisons, and short-circuit `&&`/`||`. Named component
objects support the public fields above from any app function.

This is still a C subset. Arbitrary user-defined structs/classes, inheritance,
arrays, floating point, macros, additional headers, external libraries,
`for`, `switch`, `++`, and compound assignments are not supported. Component
instances are project globals, not local variables or by-value parameters.
Use shared app functions to organize behavior. Use `i = i + 1` in loops.
`print(text)`, `ticks()`, `yield()`, and `close_window()` are also available.

Application C source is limited to 16,383 bytes. The generated project source
is bounded separately, and native code, public objects, globals, and strings
share a 24 KiB reservation. A build fails before publishing the executable if
its source, metadata, symbols, parse nesting, or output exceeds a limit.

## Verification

`make test-sdk` runs native generated-code tests, including the generated event
loop invoking Loaded, Pressed, Changed, TextChanged, and Closed handlers. It
also checks cross-function property access, invalid property writes/types,
form round trips and validation, preservation of application code, and native
control synchronization against mocked window syscalls. The native execution
tests require Linux with 32-bit syscall support.
