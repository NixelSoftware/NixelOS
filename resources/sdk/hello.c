#include <nixel.h>

void MainWindowOnLoaded(void) {
    if (MainWindow.windowLoaded) {
        Label2.text = "The window is ready";
    }
}

void Button1OnPressed(void) {
    Label2.text = TextBox1.text;
}

void Checkbox1OnChanged(void) {
    if (Checkbox1.checkboxIsTicked) {
        Label2.text = "Greetings from your C application!";
    } else {
        Label2.text = "Greeting disabled";
    }
}

void TextBox1OnTextChanged(void) {
    Label2.text = "Click Show text to display your message";
}
