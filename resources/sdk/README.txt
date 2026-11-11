Nixel on-device SDK

Launch designer to create a C window definition.
Palette: Button, Text box, Label, Checkbox.
Click a control to select; edit properties and Apply.
Click empty canvas to move a selected control.
Code opens the C editor (arrows, Home/End, Tab, Delete).
Design returns to the visual window. Save writes C. Build produces .bin. Run builds and launches.
Code outside NIXEL DESIGNER BEGIN/END markers is preserved.

Compile the example from a terminal:
ncc /usr/share/nixel/sdk/hello.c hello.bin
./hello.bin

NCC supports a small C subset: int, string handles, functions,
locals/globals, if/else, while, return, arithmetic, comparisons.
No structs, arrays, floating point, macros, pointer operations,
external headers/libraries, for, ++, or compound assignments yet.
Use i = i + 1. See nixel.h for the available SDK calls.
Sources: maximum 16383 bytes. Code/data: 24 KiB.
Designer windows: 160..600 x 100..300. Controls: maximum 48.