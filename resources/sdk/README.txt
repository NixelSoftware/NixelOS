Nixel on-device C SDK

Use designer to create a form. Give controls names such as Button1,
Checkbox1, TextBox1 and Label1. Code shows only application handlers.
The layout lives separately in a designer-owned .form file.

void Button1OnPressed(void) {
    Label1.text = TextBox1.text;
}

Other handlers: MainWindowOnLoaded, MainWindowOnClosed,
Checkbox1OnChanged and TextBox1OnTextChanged.
Public state: Button1.buttonIsPressed, Checkbox1.checkboxIsTicked,
MainWindow.windowLoaded. Access named objects from any app function.
Text and checkbox state can be assigned. Event flags are read-only.

Save writes the app .c and .form. Build creates .bin. Run launches it.
The project compiler supplies window creation and the event loop.
Do not write main() for a designer project.

Compile the example:
ncc /usr/share/nixel/sdk/hello.c hello.bin
./hello.bin
Copy BOTH hello.c and hello.form to a writable folder before editing.

C subset: functions, globals, locals, if/else, while, return,
arithmetic and named component properties. No arbitrary C classes,
user-defined structs, arrays, floating point, for, ++ or libraries.
App source: 16383 bytes. Native code/data: 24 KiB. Components: 48.
