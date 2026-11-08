#include "nixinit.h"
#include "../../kernel/drivers/serial.h"
#include "../graphics/wm/desktop.h"

#include "../../kernel/fs.h"
#include "../../kernel/task.h"
#include "../../kernel/users.h"
#include "../../kernel/keyboard.h"
#include "../../kernel/drivers/display.h"
#include "../../kernel/drivers/display/font.h"
#include "../../kernel/window.h"
#include "../../kernel/mutex.h"
#include "../services/input/input.h"
#include "../shell/commands.h"

extern void login_screen_task(void* arg);
extern void desktop_service(void* arg);
static int have_wm, have_dm;
static int console_x, console_y;

/* Manifests identify packaged, kernel-resident components, not executables. */
static int component_installed(const char* path, const char* expected) {
    char data[32];
    int size = fs_read_file(path, (uint8_t*)data, sizeof(data));
    int i = 0;
    while (expected[i]) {
        if (i >= size || data[i] != expected[i]) return 0;
        i++;
    }
    return size == i;
}

int nixinit_has_wm(void) { return have_wm; }

static void console_clear(void) {
    MUTEX_GUARD(kernel_mutex);
    display_clear(0);
    console_x = console_y = 8;
    display_blit();
}

static void console_print(const char* text) {
    MUTEX_GUARD(kernel_mutex);
    while (*text) {
        char c = *text++;
        if (c == '\n') { console_x = 8; console_y += 16; }
        else if (c == '\b') {
            if (console_x > 8) console_x -= 8;
            display_fill_rect(console_x, console_y, 8, 16, 0);
        } else if (c >= 32 && c < 127) {
            draw_char(console_x, console_y, c, 0xffffff, 1, 0);
            console_x += 8;
        }
        if (console_x + 8 > SCREEN_WIDTH) { console_x = 8; console_y += 16; }
        if (console_y + 16 > SCREEN_HEIGHT) {
            display_clear(0);
            console_x = console_y = 8;
        }
    }
    display_blit();
}

static void console_readline(char* line, unsigned capacity, int secret) {
    unsigned length = 0;
    for (;;) {
        os_input_event_t event;
        if (!input_poll_event(&event)) { task_sleep_ms(10); continue; }
        if (event.type != OS_INPUT_KEY || !event.scancode) continue;
        char c = kbd_scancode_to_ascii(event.scancode);
        if (c == '\n' || c == '\r') break;
        if (c == '\b' && length) {
            length--;
            if (!secret) console_print("\b");
        } else if (c >= 32 && c < 127 && length + 1 < capacity) {
            line[length++] = c;
            if (!secret) { char echo[2] = {c, 0}; console_print(echo); }
        }
    }
    line[length] = 0;
    console_print("\n");
}

void nixinit_session_task(void* arg) {
    if (have_wm && have_dm) {
        debug_print("[NIXINIT] Starting display manager\n");
        login_screen_task(arg);
        return;
    }
    debug_print("[NIXINIT] Starting text login\n");
    current_task->shell_output = console_print;
    console_clear();
    console_print("NixelOS systemshell\n");
    for (;;) {
        char name[64], password[64];
        console_print("login: ");
        console_readline(name, sizeof(name), 0);
        console_print("Password: ");
        console_readline(password, sizeof(password), 1);
        int authenticated = user_authenticate(name, password);
        for (unsigned i = 0; i < sizeof(password); i++)
            ((volatile char*)password)[i] = 0;
        if (!authenticated) { console_print("Login incorrect.\n"); continue; }
        const user_t* user = user_by_name(name);
        current_task->uid = user->uid;
        current_task->gid = user->gid;
        const char* home = user_home();
        unsigned i = 0;
        do { current_task->cwd[i] = home[i]; } while (home[i++]);
        break;
    }
    if (have_wm) {
        debug_print("[NIXINIT] Text login authenticated; starting WM\n");
        desktop_service(arg);
        return;
    }
    debug_print("[NIXINIT] Text login authenticated; starting systemshell\n");
    for (;;) {
        char command[256];
        console_print(user_by_id(current_task->uid)->name);
        console_print(" $ ");
        console_readline(command, sizeof(command), 0);
        shell_execute_command(command, console_print, console_clear);
    }
}

static void trace_step(const char* step) {
    debug_subsystem_print("[BOOT TRACE] ");
    debug_subsystem_print(step);
    debug_subsystem_print("\n");
}

void nixinit_start(const nixinit_services_t* services) {
    if (!services) return;
    trace_step("nixinit_start entered");
    if (services->show_status) services->show_status("Starting Desktop...", 100);
    if (services->show_status) services->show_status("Initializing IDT...", 5);
    trace_step("before IDT init");
    if (services->initialize_interrupts) services->initialize_interrupts();
    trace_step("after IDT init");
    if (services->show_status) services->show_status("Configuring PIT...", 15);
    trace_step("before PIT init");
    if (services->initialize_timer) services->initialize_timer();
    trace_step("after PIT init");
    if (services->show_status) services->show_status("Initializing ACPI...", 20);
    trace_step("before ACPI init");
    if (services->initialize_acpi) services->initialize_acpi();
    trace_step("after ACPI init");
    if (services->show_status) services->show_status("Detecting Network Adapter...", 25);
    trace_step("before network device init");
    if (services->initialize_network_device) services->initialize_network_device();
    trace_step("after network device init");
    if (services->show_status) services->show_status("Initializing Network Stack...", 35);
    trace_step("before network stack init");
    if (services->initialize_network_stack) services->initialize_network_stack();
    trace_step("after network stack init");
    if (services->show_status) services->show_status("Mounting ext4 Filesystem...", 65);
    trace_step("before filesystem mount");
    if (services->mount_filesystem) services->mount_filesystem();
    trace_step("after filesystem mount");
    have_wm = component_installed("/usr/lib/nixel/wm.component", "nixel-wm-v1\n");
    have_dm = component_installed("/usr/lib/nixel/dm.component", "nixel-dm-v1\n");
    if (services->show_status) services->show_status("Initializing Tasking...", 75);
    trace_step("before tasking init");
    if (services->initialize_tasking) services->initialize_tasking();
    trace_step("after tasking init");
    if (services->show_status) services->show_status("Loading Built-in Applications...", 85);
    trace_step("before system apps init");
    if (services->initialize_system_apps) services->initialize_system_apps();
    desktop_trace_api("after system apps init");
    trace_step("after system apps init");
    desktop_trace_api("before 95% splash update");
    if (services->show_status) services->show_status("Initializing PS/2 Mouse...", 95);
    desktop_trace_api("after 95% splash update");
    trace_step("before input init");
    if (services->initialize_input) services->initialize_input();
    trace_step("after input init");
    trace_step("before finish_boot");
    if (services->finish_boot) services->finish_boot();
    trace_step("after finish_boot");
}
