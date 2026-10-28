#include "../../include/os_version.h"
#include "../../system/user/sys_api.h"
#include <cpuid.h>

static int show_logo = 1;
static unsigned logo_row;
#include "logo.h"

static void print(const char* s) {
    while (*s) {
        uint32_t n = 0;
        while (s[n] && n < 256) ++n;
        int32_t written = sys_write(s, n);
        if (written <= 0) return;
        s += written;
    }
}

static void number(uint32_t n) {
    char buf[11];
    unsigned i = 10;
    buf[i] = 0;
    do { buf[--i] = (char)('0' + n % 10); n /= 10; } while (n);
    print(buf + i);
}

static void logo_prefix(void) {
    if (!show_logo) return;
    if (logo_row < sizeof(logo) / sizeof(logo[0])) print(logo[logo_row]);
    else for (unsigned i = 0; i < LOGO_WIDTH; ++i) print(" ");
    ++logo_row;
    print("\033[0m  ");
}

static void key(const char* name) {
    logo_prefix();
    print(name);
    print(": ");
}

static void cpu(void) {
    unsigned a, b, c, d;
    char name[49];
    unsigned max = __get_cpuid_max(0, 0);
    if (!max) { print("Unknown (CPUID unavailable)"); return; }
    unsigned ext = __get_cpuid_max(0x80000000u, 0);
    if (ext >= 0x80000004u) {
        for (unsigned i = 0; i < 3; ++i) {
            __cpuid(0x80000002u + i, a, b, c, d);
            unsigned words[4] = {a, b, c, d};
            for (unsigned j = 0; j < 16; ++j)
                name[i * 16 + j] = (char)(words[j / 4] >> (8 * (j % 4)));
        }
        name[48] = 0;
    } else {
        __cpuid(0, a, b, c, d);
        unsigned words[3] = {b, d, c};
        for (unsigned j = 0; j < 12; ++j)
            name[j] = (char)(words[j / 4] >> (8 * (j % 4)));
        name[12] = 0;
    }
    char* start = name;
    while (*start == ' ') ++start;
    unsigned len = 0;
    while (start[len]) ++len;
    while (len && start[len - 1] == ' ') start[--len] = 0;
    print(*start ? start : "Unknown");
}

static int equal(const char* s, unsigned n, const char* expected) {
    unsigned i = 0;
    while (i < n && expected[i] && s[i] == expected[i]) ++i;
    return i == n && !expected[i];
}

void user_main(const char* args) {
    show_logo = 1;
    logo_row = 0;
    while (args && *args) {
        while (*args == ' ' || *args == '\t') ++args;
        if (!*args) break;
        unsigned n = 0;
        while (args[n] && args[n] != ' ' && args[n] != '\t') ++n;
        if (equal(args, n, "--help") || equal(args, n, "-h")) {
            print("Usage: fastfetch [--logo none] [--version] [--help]\n"
                  "Freestanding OS adaptation; reports only available data.\n");
            return;
        } else if (equal(args, n, "--version")) {
            print("fastfetch OS adaptation 0.1.0 (limited feature set)\n");
            return;
        } else if (equal(args, n, "--logo")) {
            args += n;
            while (*args == ' ' || *args == '\t') ++args;
            n = 0;
            while (args[n] && args[n] != ' ' && args[n] != '\t') ++n;
            if (!equal(args, n, "none")) {
                print("fastfetch: --logo requires 'none'\n");
                sys_exit(1);
            }
            show_logo = 0;
        } else {
            print("fastfetch: unknown option; use --help\n");
            sys_exit(1);
        }
        args += n;
    }
    key("OS"); print(OS_NAME " " OS_VERSION " i386\n");
    key("Build"); print(OS_BUILD_ID "\n");
    key("Shell"); print("Built-in shell\n");
    key("Terminal"); print("System Terminal\n");
    key("CPU"); cpu(); print("\n");
    user_memory_info_t memory;
    if (sys_memory_info(&memory) == 0) {
        key("Memory");
        number(memory.used_bytes / (1024 * 1024)); print("MiB / ");
        number(memory.total_bytes / (1024 * 1024)); print("MiB\n");
    }
    while (show_logo && logo_row < sizeof(logo) / sizeof(logo[0])) {
        logo_prefix();
        print("\n");
    }

}
