#include "../../system/user/sys_api.h"

#define TELNET_PORT 2323
#define BUFFER_SIZE 512
#define MAX_CMD_LEN 256

/* Telnet Command & Option Constants */
#define IAC   255
#define DONT  254
#define DO    253
#define WONT  252
#define WILL  251

#define OPT_ECHO                 1
#define OPT_SUPPRESS_GO_AHEAD    3

/* Working Directory State */
static char session_cwd[FS_MAX_PATH] = "/";

/* --- Helper String Functions --- */

static uint32_t str_len(const char* s) {
    uint32_t len = 0;
    while (s && s[len]) len++;
    return len;
}

static int str_equal(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static int str_starts_with(const char* str, const char* prefix) {
    while (*prefix) {
        if (*str++ != *prefix++) return 0;
    }
    return 1;
}

static void str_copy(char* dest, const char* src, uint32_t cap) {
    uint32_t i = 0;
    while (src && src[i] && i + 1 < cap) {
        dest[i] = src[i];
        i++;
    }
    if (cap) dest[i] = '\0';
}

/* --- Reliable Network Transmission --- */

static int send_all(int32_t sock, const void* data, uint32_t len) {
    if (!len) return 1;
    const uint8_t* p = (const uint8_t*)data;
    while (len > 0) {
        uint32_t chunk = len > 1448 ? 1448 : len;
        int retries = 100;
        while (retries-- > 0) {
            int res = sys_tcp_send(sock, p, chunk);
            if (res == 1) {
                p += chunk;
                len -= chunk;
                break;
            }
            sys_yield();
        }
        if (retries <= 0) return 0;
    }
    return 1;
}

/* Send string with automatic \n -> \r\n expansion */
static void send_text(int32_t sock, const char* str) {
    if (!str) return;
    char buffer[256];
    uint32_t bpos = 0;

    for (uint32_t i = 0; str[i]; i++) {
        if (str[i] == '\n' && (i == 0 || str[i-1] != '\r')) {
            buffer[bpos++] = '\r';
            if (bpos >= sizeof(buffer)) {
                send_all(sock, buffer, bpos);
                bpos = 0;
            }
        }
        buffer[bpos++] = str[i];
        if (bpos >= sizeof(buffer)) {
            send_all(sock, buffer, bpos);
            bpos = 0;
        }
    }
    if (bpos > 0) {
        send_all(sock, buffer, bpos);
    }
}

static void send_prompt(int32_t sock) {
    send_text(sock, "celeste@nixel ");
    send_text(sock, session_cwd);
    send_text(sock, " > ");
}

/* --- Path Canonicalization & Resolution --- */

static void resolve_session_path(const char* in, char* out) {
    if (in[0] == '/') {
        str_copy(out, in, FS_MAX_PATH);
    } else {
        str_copy(out, session_cwd, FS_MAX_PATH);
        uint32_t len = str_len(out);
        if (len > 1 && out[len-1] != '/') {
            out[len++] = '/';
            out[len] = '\0';
        }
        str_copy(out + len, in, FS_MAX_PATH - len);
    }

    /* Normalize dot and double-dot segments */
    char temp[FS_MAX_PATH];
    str_copy(temp, out, sizeof(temp));
    
    char* src = temp;
    char* dst = out;
    *dst++ = '/';

    while (*src) {
        while (*src == '/') src++;
        if (!*src) break;

        char part[FS_MAX_FILENAME];
        uint32_t plen = 0;
        while (*src && *src != '/') {
            if (plen + 1 < sizeof(part)) part[plen++] = *src;
            src++;
        }
        part[plen] = '\0';

        if (str_equal(part, ".")) continue;
        if (str_equal(part, "..")) {
            if (dst > out + 1) {
                dst--;
                while (dst > out && *(dst-1) != '/') dst--;
            }
            continue;
        }

        if (dst > out + 1) *dst++ = '/';
        for (uint32_t i = 0; i < plen; i++) *dst++ = part[i];
    }
    if (dst == out) *dst++ = '/';
    *dst = '\0';
}

/* --- Command Implementations --- */

static void cmd_ls(int32_t sock, const char* arg) {
    char target[FS_MAX_PATH];
    while (*arg == ' ') arg++;

    if (*arg) resolve_session_path(arg, target);
    else str_copy(target, session_cwd, sizeof(target));

    char names[FS_MAX_FILES][FS_MAX_FILENAME];
    uint8_t types[FS_MAX_FILES];
    int count = sys_fs_list_dir(target, names, types);

    if (count <= 0) {
        send_text(sock, "No files found or cannot read directory.\n");
        return;
    }

    for (int i = 0; i < count; i++) {
        if (types[i] & FS_ATTR_DIRECTORY) {
            send_text(sock, "[DIR]  ");
        } else {
            send_text(sock, "       ");
        }
        send_text(sock, names[i]);
        send_text(sock, "\n");
    }
}

static void cmd_cd(int32_t sock, const char* arg) {
    while (*arg == ' ') arg++;
    char target[FS_MAX_PATH];

    if (!*arg || str_equal(arg, "~")) {
        sys_fs_home(target, sizeof(target));
    } else {
        resolve_session_path(arg, target);
    }

    /* Verify directory existence by querying directory entries */
    char names[FS_MAX_FILES][FS_MAX_FILENAME];
    uint8_t types[FS_MAX_FILES];
    int count = sys_fs_list_dir(target, names, types);

    if (count < 0) {
        send_text(sock, "cd: directory not found or permission denied.\n");
    } else {
        str_copy(session_cwd, target, sizeof(session_cwd));
    }
}

static void cmd_cat(int32_t sock, const char* arg) {
    while (*arg == ' ') arg++;
    if (!*arg) {
        send_text(sock, "Usage: cat <filename>\n");
        return;
    }
    char path[FS_MAX_PATH];
    resolve_session_path(arg, path);

    char file_buf[512];
    uint32_t offset = 0;
    int read_any = 0;

    for (;;) {
        int bytes = sys_fs_read_at(path, file_buf, sizeof(file_buf) - 1, offset);
        if (bytes <= 0) break;
        file_buf[bytes] = '\0';
        send_text(sock, file_buf);
        offset += (uint32_t)bytes;
        read_any = 1;
        if (bytes < (int)(sizeof(file_buf) - 1)) break;
    }

    if (!read_any) {
        send_text(sock, "cat: cannot read file.\n");
    } else {
        send_text(sock, "\n");
    }
}

static void cmd_mkdir(int32_t sock, const char* arg) {
    while (*arg == ' ') arg++;
    if (!*arg) {
        send_text(sock, "Usage: mkdir <directory>\n");
        return;
    }
    char path[FS_MAX_PATH];
    resolve_session_path(arg, path);

    if (sys_fs_mkdir(path) == 0) {
        send_text(sock, "Created directory.\n");
    } else {
        send_text(sock, "mkdir: failed to create directory.\n");
    }
}

static void cmd_rm(int32_t sock, const char* arg) {
    while (*arg == ' ') arg++;
    if (!*arg) {
        send_text(sock, "Usage: rm <path>\n");
        return;
    }
    char path[FS_MAX_PATH];
    resolve_session_path(arg, path);

    if (sys_fs_delete(path) == 0) {
        send_text(sock, "File deleted.\n");
    } else {
        send_text(sock, "rm: failed to delete.\n");
    }
}

static void cmd_echo(int32_t sock, const char* arg) {
    while (*arg == ' ') arg++;
    const char* redirect = 0;
    for (const char* p = arg; *p; p++) {
        if (*p == '>') { redirect = p; break; }
    }

    if (!redirect) {
        send_text(sock, arg);
        send_text(sock, "\n");
        return;
    }

    char content[128];
    uint32_t clen = (uint32_t)(redirect - arg);
    if (clen >= sizeof(content)) clen = sizeof(content) - 1;
    for (uint32_t i = 0; i < clen; i++) content[i] = arg[i];
    while (clen > 0 && content[clen-1] == ' ') clen--;
    content[clen] = '\0';

    const char* dest_file = redirect + 1;
    while (*dest_file == ' ') dest_file++;
    if (!*dest_file) {
        send_text(sock, "Usage: echo <text> > <file>\n");
        return;
    }

    char path[FS_MAX_PATH];
    resolve_session_path(dest_file, path);

    if (sys_fs_write(path, content, clen) >= 0) {
        send_text(sock, "File saved.\n");
    } else {
        send_text(sock, "echo: write failed.\n");
    }
}

/* Launch native applications in detached mode */
static int try_launch_app(int32_t sock, const char* cmd) {
    char name[FS_MAX_PATH];
    char args[128];
    uint32_t len = 0;

    while (cmd[len] && cmd[len] != ' ' && len < sizeof(name) - 1) {
        name[len] = cmd[len];
        len++;
    }
    name[len] = '\0';

    while (cmd[len] == ' ') len++;
    str_copy(args, cmd + len, sizeof(args));

    char path[FS_MAX_PATH];
    resolve_session_path(name, path);

    /* Search path if no directory component */
    const char* candidates[3];
    char p1[FS_MAX_PATH], p2[FS_MAX_PATH], p3[FS_MAX_PATH];
    str_copy(p1, path, sizeof(p1));
    str_copy(p2, "/usr/bin/", sizeof(p2));
    str_copy(p2 + 9, name, sizeof(p2) - 9);
    str_copy(p3, p2, sizeof(p3));
    uint32_t p3_len = str_len(p3);
    str_copy(p3 + p3_len, ".bin", sizeof(p3) - p3_len);

    candidates[0] = p1;
    candidates[1] = p2;
    candidates[2] = p3;

    for (int i = 0; i < 3; i++) {
        if (sys_app_launch(candidates[i], args) >= 0) {
            send_text(sock, "[Process started detached]\n");
            return 1;
        }
    }
    return 0;
}

static void handle_command(int32_t sock, const char* cmd) {
    while (*cmd == ' ') cmd++;
    if (*cmd == '\0') return;

    if (str_starts_with(cmd, "help")) {
        send_text(sock, "Builtin Commands:\n"
                        "  ls [dir]        List directory contents\n"
                        "  cd [dir]        Change working directory\n"
                        "  pwd             Print current working directory\n"
                        "  cat <file>      Display file contents\n"
                        "  echo <t> > <f>  Write text to file\n"
                        "  mkdir <dir>     Create directory\n"
                        "  rm <file>       Delete file\n"
                        "  whoami / id     Show current user context\n"
                        "  ver             Show OS version\n"
                        "  exit            Disconnect session\n"
                        "  <app.bin>       Launch binary program\n");
    } else if (str_starts_with(cmd, "ls") || str_starts_with(cmd, "dir")) {
        cmd_ls(sock, cmd + (cmd[0] == 'l' ? 2 : 3));
    } else if (str_starts_with(cmd, "cd")) {
        cmd_cd(sock, cmd + 2);
    } else if (str_equal(cmd, "pwd")) {
        send_text(sock, session_cwd);
        send_text(sock, "\n");
    } else if (str_starts_with(cmd, "cat ")) {
        cmd_cat(sock, cmd + 4);
    } else if (str_starts_with(cmd, "mkdir ")) {
        cmd_mkdir(sock, cmd + 6);
    } else if (str_starts_with(cmd, "rm ")) {
        cmd_rm(sock, cmd + 3);
    } else if (str_starts_with(cmd, "echo ")) {
        cmd_echo(sock, cmd + 5);
    } else if (str_equal(cmd, "whoami")) {
        send_text(sock, "root\n");
    } else if (str_equal(cmd, "id")) {
        send_text(sock, "uid=0(root) gid=0(root)\n");
    } else if (str_equal(cmd, "ver")) {
        send_text(sock, "NixelOS Workstation (Build 2026 i386)\n");
    } else {
        if (!try_launch_app(sock, cmd)) {
            send_text(sock, "Unknown command or binary. Type 'help'.\n");
        }
    }
}

/* --- Telnet Main Server Routine --- */

void user_main(const char* args) {
    (void)args;

    /* Detach so launching terminal regains prompt immediately */
    sys_detach();

    /* Initialize session home directory */
    sys_fs_home(session_cwd, sizeof(session_cwd));
    if (!session_cwd[0]) str_copy(session_cwd, "/", sizeof(session_cwd));

    int32_t listener = sys_tcp_listen(TELNET_PORT);
    if (listener < 0) {
        sys_exit(1);
    }

    int32_t client = -1;
    char line[MAX_CMD_LEN];
    uint32_t pos = 0;
    uint8_t buf[BUFFER_SIZE];

    for (;;) {
        /* Accept client */
        if (client < 0) {
            int32_t incoming = sys_tcp_accept(listener);
            if (incoming > 0) {
                client = incoming;
                pos = 0;

                /* Request client disable local echo and line buffering */
                uint8_t negotiate[] = {
                    IAC, WILL, OPT_ECHO,
                    IAC, WILL, OPT_SUPPRESS_GO_AHEAD
                };
                send_all(client, negotiate, sizeof(negotiate));

                send_text(client, "\r\n==========================================\r\n");
                send_text(client, "      Welcome to NixelOS Remote Shell     \r\n");
                send_text(client, "==========================================\r\n\r\n");
                send_prompt(client);
            } else {
                sys_yield();
                continue;
            }
        }

        /* Check connection state */
        int32_t state = sys_tcp_state(client);
        if (state == USER_TCP_CLOSED) {
            sys_tcp_close(client);
            client = -1;
            sys_yield();
            continue;
        }

        /* Read incoming stream */
        int32_t n = sys_tcp_read(client, buf, sizeof(buf));
        if (n > 0) {
            int32_t i = 0;
            while (i < n) {
                uint8_t c = buf[i];

                /* Filter incoming Telnet IAC commands */
                if (c == IAC) {
                    if (i + 1 < n && (buf[i + 1] >= WILL && buf[i + 1] <= DONT)) {
                        i += 3;
                    } else {
                        i++;
                    }
                    continue;
                }

                /* Enter / Return */
                if (c == '\r' || c == '\n') {
                    if (c == '\r' && (i + 1 < n) && (buf[i + 1] == '\n' || buf[i + 1] == 0)) {
                        i++;
                    }

                    line[pos] = '\0';
                    send_text(client, "\n");
                    handle_command(client, line);

                    if (line[0] == 'e' && line[1] == 'x' &&
                        line[2] == 'i' && line[3] == 't' && !line[4]) {
                        send_text(client, "Goodbye.\n");
                        sys_tcp_close(client);
                        client = -1;
                        break;
                    }

                    pos = 0;
                    if (client >= 0) {
                        send_prompt(client);
                    }
                }
                /* Backspace */
                else if (c == 0x08 || c == 0x7F) {
                    if (pos > 0) {
                        pos--;
                        send_text(client, "\b \b");
                    }
                }
                /* Normal printable keys */
                else if (c >= 32 && c <= 126) {
                    if (pos < MAX_CMD_LEN - 1) {
                        line[pos++] = (char)c;
                        char echo[2] = { (char)c, '\0' };
                        send_all(client, echo, 1);
                    }
                }
                i++;
            }
        }

        sys_yield();
    }
}