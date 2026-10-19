#include "commands.h"

static os_api_t* shell_api;
static shell_output_fn shell_output;
static shell_clear_fn shell_clear;
static char shell_args[FS_MAX_FILENAME];

static void print(const char* text) {
    if (shell_output) shell_output(text);
}

static int has_prefix(const char* text, const char* prefix) {
    while (*prefix) {
        if (*text++ != *prefix++) return 0;
    }
    return 1;
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
    if (shell_api->shell_set_output) shell_api->shell_set_output(output);

    if (command[0] == '>' && command[1] == ' ') command += 2;
    while (*command == ' ') command++;
    shell_args[0] = '\0';
    if (*command == '\0') return;

    if (has_prefix(command, "help")) {
        print("Commands:\n ls              List files\n rm              Remove files\n cat <file>      Read file\n echo <t>><file> Write file\n view <file>     Open in Viewer\n ping <IPv4>     Ping four times\n ifconfig        Show network configuration\n netdiag         Inspect network driver state\n clear           Clear screen\n");
    } else if (has_prefix(command, "clear")) {
        if (shell_clear) shell_clear();
    } else if (has_prefix(command, "rm ")) {
        const char* filename = command + 3;
        while (*filename == ' ') filename++;
        print(shell_api->fs_delete(filename) ? "File deleted.\n" : "Failed to delete file.\n");
    } else if (has_prefix(command, "ls") || has_prefix(command, "dir")) {
        char files[FS_MAX_FILES][FS_MAX_FILENAME];
        int count = shell_api->fs_list(files);
        if (count == 0) print("No files found.\n");
        else for (int i = 0; i < count; i++) { print(files[i]); print("\n"); }
    } else if (has_prefix(command, "cat ")) {
        const char* filename = command + 4;
        while (*filename == ' ') filename++;
        if (*filename == '\0') print("Usage: cat <filename>\n");
        else {
            char buffer[512];
            int bytes = shell_api->fs_read(filename, (uint8_t*)buffer, 511);
            if (bytes < 0) print("File not found!\n");
            else { buffer[bytes] = '\0'; print(buffer); print("\n"); }
        }
    } else if (has_prefix(command, "view ")) {
        const char* filename = command + 5;
        while (*filename == ' ') filename++;
        if (*filename == '\0') print("Usage: view <filename.bmp>\n");
        else if (shell_api->shell_open_viewer(filename)) {
            print("Loaded "); print(filename); print(" in Viewer\n");
        } else print("Failed to load BMP!\n");
    } else if (has_prefix(command, "ifconfig")) {
        if (shell_api->shell_run_ifconfig) shell_api->shell_run_ifconfig();
    } else if (has_prefix(command, "netdiag")) {
        if (shell_api->shell_run_netdiag) shell_api->shell_run_netdiag();
    } else if (has_prefix(command, "ping ")) {
        if (shell_api->shell_run_ping) shell_api->shell_run_ping(command + 5);
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
            else if (shell_api->fs_write(filename, (const uint8_t*)contents, length) >= 0) {
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
        while (command[length] && out < FS_MAX_FILENAME - 1) shell_args[out++] = command[length++];
        shell_args[out] = '\0';
        int result = shell_api->shell_launch_app ?
            shell_api->shell_launch_app(name, shell_args) : -1;
        if (result == 0) return;
        if (result == -2) print("Invalid executable format.\n");
        else print("Unknown cmd. Type 'help'\n");
    }
}

void shell_initialize(os_api_t* api) {
    shell_api = api;
}
