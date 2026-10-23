#include "../../kernel/mutex.h"
#include "commands.h"
#include "network_commands.h"
#include "../loader/app_loader.h"
#include "../graphics/terminal/terminal.h"

static os_api_t* shell_api;
static shell_output_fn shell_output;
static shell_clear_fn shell_clear;
#define SHELL_ARGUMENT_MAX 256
static char shell_args[SHELL_ARGUMENT_MAX];

static void print(const char* text) {
    if (shell_output) shell_output(text);
}

static int has_prefix(const char* text, const char* prefix) {
    while (*prefix) {
        if (*text++ != *prefix++) return 0;
    }
    return 1;
}

static void print_hex_u16(uint16_t val) {
    const char hex_chars[] = "0123456789ABCDEF";
    char out[7] = "0x0000";
    for (int i = 3; i >= 0; i--) {
        out[2 + (3 - i)] = hex_chars[(val >> (i * 4)) & 0x0F];
    }
    print(out);
}

static void print_u32(uint32_t value) {
    char digits[11];
    int count = 0;
    if (value == 0) {
        print("0");
        return;
    }
    while (value != 0) {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    }
    while (count > 0) {
        char digit[2] = { digits[--count], '\0' };
        print(digit);
    }
}

const char* shell_command_args(void) {
    return shell_args;
}

void shell_execute_command(const char* command, shell_output_fn output,
                           shell_clear_fn clear) {
    shell_output = output;
    shell_clear = clear;
    if (!shell_api) return;
    if (shell_api->shell.shell_set_output) shell_api->shell.shell_set_output(output);

    if (command[0] == '>' && command[1] == ' ') command += 2;
    while (*command == ' ') command++;
    shell_args[0] = '\0';
    if (*command == '\0') return;

    if (has_prefix(command, "help")) {
        print("Commands:\n"
                    " ps              Show processes and parent PIDs\n"
                    " ls              List files\n"
                    " rm              Remove files\n"
                    " cat <file>      Read file\n"
                    " echo <t>><file> Write file\n"
                    " view <file>     Open in Viewer\n"
                    " ping <host>     Ping an IPv4 address or DNS hostname\n"
                    " ifconfig        Show network configuration\n"
                    " netdev          Show detected network hardware\n"
                    " clear           Clear screen\n");
    } else if (has_prefix(command, "ps")) {
        task_info_t processes[MAX_TASKS];
        int count = task_snapshot(processes, MAX_TASKS);
        print("PID  PPID  NAME\n");
        for (int i = 0; i < count; i++) {
            print_u32(processes[i].pid); print("  ");
            print_u32(processes[i].ppid); print("  ");
            print(processes[i].name); print("\n");
        }
    } else if (has_prefix(command, "clear")) {
        if (shell_clear) shell_clear();
    } else if (has_prefix(command, "rm ")) {
        const char* filename = command + 3;
        while (*filename == ' ') filename++;
        print(shell_api->fs.delete_file(filename) ? "File deleted.\n" : "Failed to delete file.\n");
    } else if (has_prefix(command, "ls") || has_prefix(command, "dir")) {
        char files[FS_MAX_FILES][FS_MAX_FILENAME];
        int count = shell_api->fs.list(files);
        if (count == 0) print("No files found.\n");
        else for (int i = 0; i < count; i++) { print(files[i]); print("\n"); }
    } else if (has_prefix(command, "cat ")) {
        const char* filename = command + 4;
        while (*filename == ' ') filename++;
        if (*filename == '\0') print("Usage: cat <filename>\n");
        else {
            char buffer[512];
            int bytes = shell_api->fs.read(filename, (uint8_t*)buffer, 511);
            if (bytes < 0) print("File not found!\n");
            else { buffer[bytes] = '\0'; print(buffer); print("\n"); }
        }
    } else if (has_prefix(command, "view ")) {
        const char* filename = command + 5;
        while (*filename == ' ') filename++;
        if (*filename == '\0') print("Usage: view <filename.bmp>\n");
        else if (shell_api->shell.shell_open_viewer(filename)) {
            print("Loaded "); print(filename); print(" in Viewer\n");
        } else print("Failed to load BMP!\n");
    } else if (has_prefix(command, "ifconfig")) {
        shell_network_ifconfig(shell_output);
    } else if (has_prefix(command, "netdev")) {
        if (!shell_api || !shell_api->network.get_device_info) {
            print("Network API unavailable.\n");
            return;
        }

        uint16_t vendor = 0;
        uint16_t device = 0;
        const char* name = 0;
        shell_api->network.get_device_info(&vendor, &device, &name);

        if (vendor == 0 && device == 0) {
            print("No supported PCI network adapter found.\n");
        } else {
            print("PCI Network Device:\n  Name:   ");
            print(name ? name : "Unknown");
            print("\n  Vendor: ");
            print_hex_u16(vendor);
            print("\n  Device: ");
            print_hex_u16(device);
            print("\n");
        }
    } else if (has_prefix(command, "echo ")) {
        const char* text = command + 5;
        const char* redirect = 0;
        for (const char* scan = text; *scan; scan++) if (*scan == '>') { redirect = scan; break; }
        if (!redirect) { print(text); print("\n"); }
        else {
            char contents[128];
            int length = (int)(redirect - text);
            if (length > 127) length = 127;
            for (int i = 0; i < length; i++) contents[i] = text[i];
            while (length > 0 && contents[length - 1] == ' ') length--;
            contents[length] = '\0';
            const char* filename = redirect + 1;
            while (*filename == ' ') filename++;
            if (*filename == '\0') print("Usage: echo <text> > <file>\n");
            else if (shell_api->fs.write(filename, (const uint8_t*)contents, length) >= 0) {
                print("Saved to "); print(filename); print("\n");
            } else print("Write failed!\n");
        }
    } else {
        char name[FS_MAX_FILENAME];
        int length = 0;
        while (command[length] && command[length] != ' ' && length < FS_MAX_FILENAME - 1) {
            name[length] = command[length];
            length++;
        }
        name[length] = '\0';
        while (command[length] == ' ') length++;
        int out = 0;
        while (command[length] && out < SHELL_ARGUMENT_MAX - 1)
            shell_args[out++] = command[length++];
        shell_args[out] = '\0';
        int result = app_loader_launch_wait(shell_api, name, shell_args);
        if (result == 0) return;
        if (result == -2) print("Invalid executable format.\n");
        else print("Unknown cmd. Type 'help'\n");
    }
}

void shell_initialize(os_api_t* api) {
    shell_api = api;
    shell_network_initialize(api);
}

/* Commands run on a worker stack. Input callbacks only copy a bounded request;
 * disk waits and application preparation never retain the input callback lock. */
#define SHELL_QUEUE_SIZE 8u
typedef struct {
    char command[SHELL_ARGUMENT_MAX];
    shell_output_fn output;
    shell_clear_fn clear;
} shell_request_t;
static shell_request_t shell_queue[SHELL_QUEUE_SIZE];
static uint32_t shell_head, shell_count;
static task_t* terminal_task;
void shell_close_terminal(void) {
    MUTEX_GUARD(kernel_mutex);
    shell_count = 0;
    if (terminal_task) task_terminate_children(terminal_task->id);
}

int shell_submit_command(const char* command, shell_output_fn output, shell_clear_fn clear) {
    MUTEX_GUARD(kernel_mutex);
    if (!command) return 0;
    uint32_t length = 0;
    while (command[length] && length < SHELL_ARGUMENT_MAX) length++;
    if (length == SHELL_ARGUMENT_MAX || shell_count == SHELL_QUEUE_SIZE) return 0;

    shell_request_t* request = &shell_queue[(shell_head + shell_count) % SHELL_QUEUE_SIZE];
    for (uint32_t i = 0; i <= length; i++) request->command[i] = command[i];
    request->output = output; request->clear = clear;
    shell_count++;
    return 1;
}

void shell_service(void* argument) {
    (void)argument;
    terminal_task = current_task;
    for (;;) {
        shell_request_t request;
        mutex_lock(&kernel_mutex);
        if (!shell_count) {
            mutex_unlock(&kernel_mutex);
            task_sleep_ms(1);
            continue;
        }
        request = shell_queue[shell_head];
        shell_head = (shell_head + 1u) % SHELL_QUEUE_SIZE;
        shell_count--;
        mutex_unlock(&kernel_mutex);
        shell_execute_command(request.command, request.output, request.clear);
        terminal_command_done();
    }
}
