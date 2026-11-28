#include "shell.h"
#include "fbcon.h"
#include "virtio_input.h"
#include "task.h"
#include "timer.h"

#define LINE_MAX 78u

static char line[LINE_MAX + 1u];
static uint32_t length;

static void put_dec(uint32_t value) {
    char digits[10];
    int n = 0;
    if (!value) {
        fbcon_putc('0');
        return;
    }
    while (value) {
        digits[n++] = (char)('0' + value % 10u);
        value /= 10u;
    }
    while (n) fbcon_putc(digits[--n]);
}

static void prompt(void) {
    fbcon_write("> ");
}

static int is_command(const char* text, const char* name) {
    while (*name)
        if (*text++ != *name++)
            return 0;
    return *text == 0 || *text == ' ';
}

static const char* skip_space(const char* text) {
    while (*text == ' ') text++;
    return text;
}

static void execute(void) {
    line[length] = 0;
    const char* command = skip_space(line);
    if (!*command) return;

    if (is_command(command, "help")) {
        fbcon_write("commands: help echo tasks ticks clear\n");
    } else if (is_command(command, "echo")) {
        fbcon_write(skip_space(command + 4));
        fbcon_write("\n");
    } else if (is_command(command, "tasks")) {
        task_info_t info[8];
        int count = task_snapshot(info, 8);
        for (int i = 0; i < count; i++) {
            fbcon_write("pid ");
            put_dec(info[i].pid);
            fbcon_write(" state ");
            put_dec(info[i].state);
            fbcon_write(" ");
            fbcon_write(info[i].name);
            fbcon_write("\n");
        }
    } else if (is_command(command, "ticks")) {
        uint32_t ms = (uint32_t)(arch_timer_ticks() / 1000000ull);
        put_dec(ms);
        fbcon_write(" ms\n");
    } else if (is_command(command, "clear")) {
        fbcon_clear(0x00101828u);
    } else {
        fbcon_write("unknown: ");
        fbcon_write(command);
        fbcon_write("\n");
    }
}

void shell_init(void) {
    length = 0;
    prompt();
    fbcon_flush();
}

void shell_poll(void) {
    char buffer[16];
    int count = virtio_input_poll(buffer, sizeof(buffer));
    if (count <= 0) return;

    int submitted = 0;
    for (int i = 0; i < count; i++) {
        char c = buffer[i];
        if (c == '\r' || c == '\n') {
            fbcon_write("\n");
            execute();
            length = 0;
            submitted = 1;
        } else if (c == '\b' || c == 0x7f) {
            if (length) {
                length--;
                fbcon_putc('\b');
            }
        } else if (c >= 0x20 && c < 0x7f) {
            if (length < LINE_MAX) {
                line[length++] = c;
                fbcon_putc(c);
            }
        }
    }
    if (submitted) prompt();
    fbcon_flush();
}
