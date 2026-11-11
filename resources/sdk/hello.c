#include <nixel.h>

/* NIXEL DESIGNER BEGIN */
void MainWindow(void) {
    window("Hello from C", 400, 240);
    label(1, 16, 40, 350, 24, "Write an app directly on NixelOS");
    textbox(2, 16, 80, 260, 28, "Hello, world!");
    button(3, 16, 124, 120, 28, "Show text");
    checkbox(4, 154, 124, 200, 28, "Enable greeting");
    label(5, 16, 176, 350, 24, "Click Show text");
}
/* NIXEL DESIGNER END */

int main(void) {
    MainWindow();
    int event = 0;
    while (event >= 0) {
        event = poll();
        if (event == 3) {
            set_text(5, get_text(2));
        }
        if (event == 4 && checked(4)) {
            set_text(5, "Greetings from your C application!");
        }
    }
    return 0;
}
