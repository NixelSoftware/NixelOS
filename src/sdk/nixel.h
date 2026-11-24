#ifndef NIXEL_SDK_H
#define NIXEL_SDK_H
#include "components.h"
/* The designer publishes named Window, Button, Checkbox, TextBox and Label
 * instances to every function in the application's .c file.
 *
 * void Button1OnPressed(void) { Label1.text = TextBox1.text; }
 * void Checkbox1OnChanged(void) { if(Checkbox1.checkboxIsTicked) { ... } }
 * void MainWindowOnLoaded(void) { if(MainWindow.windowLoaded) { ... } }
 *
 * The project compiler supplies layout, startup and the event loop privately.
 * Handlers are ordinary C functions with no parameters. */
int close_window(void);
int print(char *text);
int ticks(void);
int yield(void);
#endif
