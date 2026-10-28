#include "../../include/os_version.h"
#include "../../kernel/mutex.h"
#include "commands.h"
#include "../../kernel/users.h"
#include "../../kernel/fs.h"
#include "network_commands.h"
#include "../loader/app_loader.h"
#include "../graphics/terminal/terminal.h"

static os_api_t* shell_api;
#define shell_output (current_task->shell_output)
#define shell_clear (current_task->shell_clear)
#define SHELL_ARGUMENT_MAX 256
#define shell_args (current_task->command_args)

/* Each terminal owns a queue and shell task; no command state is shared. */
typedef struct {
    terminal_t* terminal;
    task_t* task;
    char command[SHELL_ARGUMENT_MAX];
    char cwd[FS_MAX_PATH];
    shell_output_fn output;
    shell_clear_fn clear;
    uint16_t uid, gid;
    uint8_t allocated, pending, active, closing;
} shell_session_t;
static shell_session_t sessions[TERMINAL_MAX_INSTANCES];
static shell_session_t* session_for(terminal_t* terminal) {
    for (int i = 0; i < TERMINAL_MAX_INSTANCES; i++)
        if (sessions[i].allocated && sessions[i].terminal == terminal) return &sessions[i];
    return 0;
}

static void copy_text(char* out, const char* in, unsigned capacity) {
    unsigned i = 0;
    while (in[i] && i + 1 < capacity) { out[i] = in[i]; i++; }
    out[i] = 0;
}
static int equal(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
static shell_session_t* active_session(void) {
    return current_task ? session_for(current_task->terminal) : 0;
}
/* Resolve relative paths and dot segments, requiring exact directory-entry
 * spelling. FAT remains compatible with existing applications. */
static int resolve_path(const char* input, char out[FS_MAX_PATH], int create) {
    shell_session_t* session = active_session();
    copy_text(out, input[0] == '/' ? "/" : session ? session->cwd : "/", FS_MAX_PATH);
    if (!out[0]) copy_text(out, "/", FS_MAX_PATH);
    unsigned length = 0; while (out[length]) length++;
    const char* scan = input;
    while (*scan) {
        while (*scan == '/') scan++;
        if (!*scan) break;
        char part[FS_MAX_FILENAME]; unsigned n = 0;
        while (*scan && *scan != '/') {
            if (n + 1 >= sizeof(part)) return 0;
            part[n++] = *scan++;
        }
        part[n] = 0;
        if (equal(part, ".")) continue;
        if (equal(part, "..")) {
            while (length > 1 && out[length-1] != '/') length--;
            if (length > 1) length--;
            out[length] = 0; continue;
        }
        char names[FS_MAX_FILES][FS_MAX_FILENAME];
        int count = fs_list_dir(out, names), found = 0;
        for (int i = 0; i < count; i++) if (equal(part, names[i])) { found = 1; break; }
        const char* rest = scan; while (*rest == '/') rest++;
        if (!found && (!create || *rest)) return 0;
        /* Refuse a differently-cased alias when creating a file. */
        if (!found && create) {
            unsigned base = 0, extension = 0; int dot = 0;
            for (unsigned i = 0; i < n; i++) {
                if (part[i] == '.') { if (dot || !base) return 0; dot = 1; }
                else {
                    if (part[i] <= ' ' || part[i] == ':' || part[i] == '*' || part[i] == '?') return 0;
                    if (dot) extension++; else base++;
                }
            }
            if (!base || base > 8 || extension > 3 || (dot && !extension)) return 0;
            char candidate[FS_MAX_PATH];
            copy_text(candidate, out, sizeof(candidate)); unsigned k = length;
            if (k > 1) candidate[k++] = '/';
            if (k + n >= sizeof(candidate)) return 0;
            copy_text(candidate+k, part, sizeof(candidate)-k);
            fs_stat_t st; if (fs_stat(candidate, &st)) return 0;
        }
        if (length + n + 1 >= FS_MAX_PATH) return 0;
        if (length > 1) out[length++] = '/';
        copy_text(out+length, part, FS_MAX_PATH-length); length += n;
        if ((*rest || *scan == '/') && !fs_is_directory(out)) return 0;
    }
    return 1;
}
void shell_format_prompt(terminal_t* terminal, char* out, unsigned capacity) {
    shell_session_t* session = session_for(terminal);
    uint16_t uid = session ? session->uid : USER_REGULAR;
    if (session && session->task) uid = session->task->uid;
    const user_t* user = user_by_id(uid);
    const char* cwd = session && session->cwd[0] ? session->cwd : "/";
    unsigned length = 0; while (cwd[length]) length++;
    /* Keep the directory tail and prompt marker visible for deep paths. */
    if (length > 64) cwd += length - 64;
    const char* marker = user && user->uid == USER_ROOT ? " # " : " > ";
    const char* parts[] = {user ? user->name : "unknown", "@nixel ", cwd, marker};
    unsigned n = 0;
    for (unsigned p = 0; p < 4; p++) for (unsigned i = 0; parts[p][i] && n+1 < capacity; i++) out[n++] = parts[p][i];
    if (capacity) out[n] = 0;
}

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

static int account_command(const char* command) {
    if (has_prefix(command, "whoami") && !command[6]) {
        const user_t* u = user_by_id(user_uid());
        print(u ? u->name : "unknown"); print("\n");
    } else if (has_prefix(command, "id") && !command[2]) {
        print("uid="); print_u32(user_uid()); print(" gid="); print_u32(user_gid()); print("\n");
    } else if (has_prefix(command, "users") && !command[5]) {
        for (unsigned i = 0; i < USER_COUNT; i++) {
            print(users[i].name);
            if (i + 1 < USER_COUNT) print(" ");
        }
        print("\n");
    } else if (equal(command, "su")) {
        print("Usage: su <account>\n");
    } else if (has_prefix(command, "su ")) {
        const char* target = command + 3;
        while (*target == ' ') target++;
        if (!*target || !user_switch(target))
            print("Cannot switch user (root required; account must exist).\n");
    } else if (has_prefix(command, "stat ")) {
        fs_stat_t st;
        char path[FS_MAX_PATH];
        if (!resolve_path(command + 5, path, 0) || !fs_stat(path, &st)) print("Cannot stat path.\n");
        else {
            const user_t* u = user_by_id(st.uid);
            print("owner="); print(u ? u->name : "unknown");
            print(" uid="); print_u32(st.uid); print(" gid="); print_u32(st.gid);
            char mode[5] = {'0', '0' + ((st.mode >> 6) & 7), '0' + ((st.mode >> 3) & 7), '0' + (st.mode & 7), 0};
            print(" mode="); print(mode); print("\n");
        }
    } else if (has_prefix(command, "chmod ") || has_prefix(command, "chown ")) {
        int owner = command[2] == 'o';
        const char* arg = command + 6;
        char token[32]; int n = 0;
        while (*arg && *arg != ' ' && n < 31) token[n++] = *arg++;
        token[n] = 0;
        while (*arg == ' ') arg++;
        int ok = 0;
        if (owner) {
            const user_t* u = user_by_name(token);
            char path[FS_MAX_PATH];
            if (u && *arg && resolve_path(arg, path, 0)) ok = fs_chown(path, u->uid);
        } else {
            unsigned mode = 0; int valid = n == 3 || (n == 4 && token[0] == '0');
            for (int i = 0; i < n; i++) {
                if (token[i] < '0' || token[i] > '7') valid = 0;
                mode = (mode << 3) | (token[i] - '0');
            }
            char path[FS_MAX_PATH];
            if (valid && *arg && resolve_path(arg, path, 0)) ok = fs_chmod(path, mode);
        }
        if (!ok) print("Operation failed: check arguments, ownership and permissions.\n");
    } else return 0;
    return 1;
}

void shell_execute_command(const char* command, shell_output_fn output,
                           shell_clear_fn clear) {
    shell_output = output;
    shell_clear = clear;
    if (!shell_api) return;
    if (shell_api->shell.shell_set_output) shell_api->shell.shell_set_output(output);

    if (command[0] == '>' && command[1] == ' ') command += 2;
    while (*command == ' ') command++;
    char path[FS_MAX_PATH];
    shell_args[0] = '\0';
    if (*command == '\0') return;

    if (has_prefix(command, "sudo") && (!command[4] || command[4] == ' ')) {
        const char* elevated = command + 4;
        while (*elevated == ' ') elevated++;
        if (!*elevated) { print("Usage: sudo <command>\n"); return; }
        int login_shell = equal(elevated, "-i") || equal(elevated, "su");
        uint16_t uid = user_uid(), gid = user_gid();
        if (uid != USER_ROOT) {
            if (!user_can_logon(user_by_id(uid))) {
                print("sudo: this account is not allowed.\n");
                return;
            }
            char password[64] = {0};
            print("[sudo] root password: ");
            int entered = terminal_read_password(password, sizeof(password));
            int authenticated = entered && user_sudo_begin(password);
            for (unsigned i = 0; i < sizeof(password); i++) ((volatile char*)password)[i] = 0;
            if (!authenticated) { print("sudo: authentication failed or cancelled.\n"); return; }
        }
        if (login_shell) {
            current_task->uid = USER_ROOT;
            current_task->gid = USER_ROOT;
            print("Started root login shell.\n");
        } else {
            shell_execute_command(elevated, output, clear);
            current_task->uid = uid;
            current_task->gid = gid;
        }
        return;
    }
    if (account_command(command)) return;
    if ((has_prefix(command, "help") && (!command[4] || command[4] == ' '))) {
        print("Commands:\n"
                    " ver             Show OS version and build ID\n"
                    " users / whoami / id   Show accounts or identity\n"
                    " sudo <command>  Run as root (password required)\n"
                    " su <account>    Switch identity (root only)\n"
                    " stat <path>     Show ownership and mode\n"
                    " chmod <octal> <path>  Set permissions\n"
                    " chown <account> <path> Set owner (root only)\n"
                    " ps              Show processes and parent PIDs\n"
                    " com0            Follow kernel serial/audio logs\n"
                    " ls [path]       List directory or file (exact case)\n"
                    " cd [dir]        Change directory (default /)\n"
                    " mkdir <dir>     Create a directory\n"
                    " pwd             Show current directory\n"
                    " rm              Remove files\n"
                    " cat <file>      Read file\n"
                    " echo <t>><file> Write file\n"
                    " view <file>     Open in Viewer\n"
                    " ping <host>     Ping an IPv4 address or DNS hostname\n"
                    " ifconfig        Show network configuration\n"
                    " netdev          Show detected network hardware\n"
                    " clear           Clear screen\n");
    } else if (has_prefix(command, "ver") && (!command[3] || command[3] == ' ')) {
        print(OS_VERSION_DESCRIPTION "\n");
    } else if ((has_prefix(command, "ps") && (!command[2] || command[2] == ' '))) {
        task_info_t processes[MAX_TASKS];
        int count = task_snapshot(processes, MAX_TASKS);
        print("PID  PPID  NAME\n");
        for (int i = 0; i < count; i++) {
            print_u32(processes[i].pid); print("  ");
            print_u32(processes[i].ppid); print("  ");
            print(processes[i].name); print("\n");
        }
    } else if ((has_prefix(command, "clear") && (!command[5] || command[5] == ' '))) {
        if (shell_clear) shell_clear();
    } else if (has_prefix(command, "rm ")) {
        const char* filename = command + 3;
        while (*filename == ' ') filename++;
        print(resolve_path(filename, path, 0) && shell_api->fs.delete_file(path) ? "File deleted.\n" : "Failed to delete file.\n");
    } else if ((has_prefix(command, "ls") && (!command[2] || command[2] == ' ')) ||
               (has_prefix(command, "dir") && (!command[3] || command[3] == ' '))) {
        const char* arg = command + (command[0] == 'l' ? 2 : 3);
        while (*arg == ' ') arg++;
        fs_stat_t st;
        if (!resolve_path(arg, path, 0) || !fs_stat(path, &st) || !fs_access(path, FS_ACCESS_READ))
            print("ls: path not found or permission denied.\n");
        else if (equal(path, "/") || fs_is_directory(path)) {
            char files[FS_MAX_FILES][FS_MAX_FILENAME]; int count = fs_list_dir(path, files);
            if (!count) print("No files found.\n");
            for (int i = 0; i < count; i++) { print(files[i]); print("\n"); }
        } else { print(path); print("\n"); }
    } else if (has_prefix(command, "mkdir") && (!command[5] || command[5] == ' ')) {
        const char* arg = command + 5; while (*arg == ' ') arg++;
        char directory[FS_MAX_PATH]; copy_text(directory, arg, sizeof(directory));
        unsigned length = 0; while (directory[length]) length++;
        while (length && directory[length-1] == ' ') directory[--length] = 0;
        while (length > 1 && directory[length-1] == '/') directory[--length] = 0;
        fs_stat_t st;
        if (!length) print("Usage: mkdir <directory>\n");
        else if (!resolve_path(directory, path, 1))
            print("mkdir: invalid name, missing parent, case mismatch, or permission denied.\n");
        else if (fs_stat(path, &st)) print("mkdir: path already exists.\n");
        else if (fs_mkdir(path) < 0) print("mkdir: cannot create directory (permission denied or disk error).\n");
        else { print("Created "); print(path); print("\n"); }
    } else if (has_prefix(command, "cd") && (!command[2] || command[2] == ' ')) {
        const char* arg = command + 2; while (*arg == ' ') arg++;
        shell_session_t* session = active_session();
        if (!*arg) arg = "/";
        if (!session || !resolve_path(arg, path, 0) ||
            (!equal(path, "/") && !fs_is_directory(path)) || !fs_access(path, FS_ACCESS_EXEC))
            print("cd: directory not found (case sensitive) or permission denied.\n");
        else copy_text(session->cwd, path, sizeof(session->cwd));
    } else if (equal(command, "pwd")) {
        shell_session_t* session = active_session(); print(session ? session->cwd : "/"); print("\n");
    } else if (has_prefix(command, "cat ")) {
        const char* filename = command + 4;
        while (*filename == ' ') filename++;
        if (*filename == '\0') print("Usage: cat <filename>\n");
        else {
            char buffer[512];
            int bytes = resolve_path(filename, path, 0) ? shell_api->fs.read(path, (uint8_t*)buffer, 511) : -1;
            if (bytes < 0) print("Cannot read file (missing or permission denied).\n");
            else { buffer[bytes] = '\0'; print(buffer); print("\n"); }
        }
    } else if (has_prefix(command, "view ")) {
        const char* filename = command + 5;
        while (*filename == ' ') filename++;
        if (*filename == '\0') print("Usage: view <filename.bmp>\n");
        else if (resolve_path(filename, path, 0) && shell_api->shell.shell_open_viewer(path)) {
            print("Loaded "); print(filename); print(" in Viewer\n");
        } else print("Failed to load BMP!\n");
    } else if ((has_prefix(command, "ifconfig") && (!command[8] || command[8] == ' '))) {
        shell_network_ifconfig(shell_output);
    } else if ((has_prefix(command, "netdev") && (!command[6] || command[6] == ' '))) {
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
            else if (resolve_path(filename, path, 1) && shell_api->fs.write(path, (const uint8_t*)contents, length) >= 0) {
                print("Saved to "); print(filename); print("\n");
            } else print("Write failed!\n");
        }
    } else {
        char name[FS_MAX_PATH];
        int length = 0;
        while (command[length] && command[length] != ' ' && length < FS_MAX_PATH - 1) {
            name[length] = command[length];
            length++;
        }
        name[length] = '\0';
        while (command[length] == ' ') length++;
        int out = 0;
        while (command[length] && out < SHELL_ARGUMENT_MAX - 1)
            shell_args[out++] = command[length++];
        shell_args[out] = '\0';
        int found = resolve_path(name, path, 0);
        char candidate[FS_MAX_PATH];
        unsigned n = 0; while (name[n]) n++;
        const char* extensions[] = {"", ".bin", ".BIN"};
        for (int root = 0; root < 2 && !found; root++) {
            if (root && name[0] == '/') break;
            for (int ext = 0; ext < 3 && !found; ext++) {
                unsigned prefix = root ? 1 : 0;
                if (n + prefix + (ext ? 4 : 0) >= sizeof(candidate)) continue;
                if (root) candidate[0] = '/';
                copy_text(candidate+prefix, name, sizeof(candidate)-prefix);
                copy_text(candidate+prefix+n, extensions[ext], sizeof(candidate)-prefix-n);
                found = resolve_path(candidate, path, 0);
            }
        }
        int result = found ? app_loader_launch_wait(shell_api, path, shell_args) : -1;
        if (result == 0) return;
        if (result == -2) print("Invalid executable format.\n");
        else print("Unknown cmd. Type 'help'\n");
    }
}

void shell_initialize(os_api_t* api) {
    shell_api = api;
    shell_network_initialize(api);
}

int shell_open_terminal(terminal_t* terminal) {
    MUTEX_GUARD(kernel_mutex);
    for (int i = 0; i < TERMINAL_MAX_INSTANCES; i++) {
        shell_session_t* s = &sessions[i];
        if (s->allocated) continue;
        *s = (shell_session_t){
            .terminal = terminal,
            .uid = current_task ? current_task->uid : USER_REGULAR,
            .gid = current_task ? current_task->gid : USER_REGULAR,
            .allocated = 1,
            .cwd = "/"
        };
        if (task_create(shell_service, s, "terminal") < 0) { s->allocated = 0; return -1; }
        return 0;
    }
    return -1;
}
int shell_interrupt(terminal_t* terminal) {
    MUTEX_GUARD(kernel_mutex);
    shell_session_t* s = session_for(terminal);
    if (!s) return 0;
    s->pending = 0;
    if (s->task) {
        if (s->active) s->task->terminate_requested = 1;
        task_terminate_children(s->task->id);
    }
    return s->active;
}
void shell_close_terminal(terminal_t* terminal) {
    MUTEX_GUARD(kernel_mutex);
    shell_session_t* s = session_for(terminal);
    if (!s) return;
    s->closing = 1;
    shell_interrupt(terminal);
}
int shell_submit_command(terminal_t* terminal, const char* command,
                         shell_output_fn output, shell_clear_fn clear) {
    MUTEX_GUARD(kernel_mutex);
    shell_session_t* s = session_for(terminal);
    if (!s || s->closing || s->pending || s->active || !command) return 0;
    uint32_t length = 0;
    while (command[length] && length < SHELL_ARGUMENT_MAX) length++;
    if (length == SHELL_ARGUMENT_MAX) return 0;
    for (uint32_t i = 0; i <= length; i++) s->command[i] = command[i];
    s->output = output; s->clear = clear; s->pending = 1;
    return 1;
}
void shell_service(void* argument) {
    shell_session_t* s = argument;
    mutex_lock(&kernel_mutex);
    s->task = current_task;
    current_task->terminal = s->terminal;
    /* Keep the UID/GID inherited from the authenticated WM session. */
    /* A new shell is independent of the terminal that launched it. */
    current_task->detached = 0;
    mutex_unlock(&kernel_mutex);
    for (;;) {
        mutex_lock(&kernel_mutex);
        if (current_task->terminate_requested) s->closing = 1;
        if (s->closing) {
            terminal_release(s->terminal);
            s->allocated = 0;
            mutex_unlock(&kernel_mutex);
            return;
        }
        if (!s->pending) {
            mutex_unlock(&kernel_mutex);
            task_sleep_ms(1);
            continue;
        }
        char command[SHELL_ARGUMENT_MAX];
        for (unsigned i = 0; i < sizeof(command); i++) command[i] = s->command[i];
        s->pending = 0; s->active = 1;
        current_task->terminate_requested = 0;
        mutex_unlock(&kernel_mutex);
        shell_execute_command(command, s->output, s->clear);
        mutex_lock(&kernel_mutex);
        current_task->terminate_requested = 0;
        s->active = 0;
        if (!s->closing) terminal_command_done(s->terminal);
        mutex_unlock(&kernel_mutex);
    }
}
