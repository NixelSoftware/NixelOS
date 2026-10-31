/* Execute the production dispatcher and loader on a 32-bit Linux host.
 * Hardware and scheduling are mocked; unexercised dependencies trap. */
#include "../../system/loader/app_loader.c"
#include "../../system/graphics/dialog/shutdown.c"
#include "../drivers/floppy.h"
#include "../drivers/power.h"
#include "../net/tcp.h"
#include "../users.h"

static void print(const char* text) {
    unsigned n = 0; while (text[n]) n++;
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(text), "d"(n) : "memory");
}
static void finish(int status) __attribute__((noreturn));
static void finish(int status) {
    __asm__ volatile("int $0x80" : : "a"(1), "b"(status) : "memory");
    __builtin_unreachable();
}
#define CHECK(condition) do { if (!(condition)) { print("FAIL: " #condition "\n"); finish(1); } } while (0)
void unexpected_dependency(void) { print("FAIL: unexpected subsystem call\n"); finish(1); }

mutex_t kernel_mutex;
static task_t shell_task, child_task;
task_t* current_task = &shell_task;
volatile uint32_t arch_user_exit_requested;
volatile uint32_t frame_timer;
static unsigned side_effects, user_entries, destroyed, writes, deletes;
static unsigned shutdown_calls, reboot_calls, create_calls;
int32_t screen_width = 800, screen_height = 600;
static int delete_result;
static uint16_t expected_uid;
static uint8_t executable_type;
static uint32_t directory[1024] __attribute__((aligned(4096)));
static uint32_t table[1024] __attribute__((aligned(4096)));
static uint32_t active_directory;
static void (*queued_entry)(void*);
static void* queued_argument;
static void* exit_context[5];

void mutex_lock(mutex_t* m) { (void)m; }
void mutex_unlock(mutex_t* m) { (void)m; }
void debug_print(const char* text) { (void)text; }
void debug_hex(uint32_t n) { (void)n; }
void kernel_user_write(const char* text, uint32_t n) { (void)text; (void)n; writes++; }
void power_shutdown(void) { side_effects++; shutdown_calls++; }
void power_reboot(void) { side_effects++; reboot_calls++; }
int floppy_init(void) { side_effects++; return 1; }
int floppy_read_sectors(uint32_t lba, uint8_t count, uint8_t* buffer) {
    (void)lba; (void)count; buffer[0] = 42; side_effects++; return 1;
}
int floppy_write_sectors(uint32_t lba, uint8_t count, const uint8_t* buffer) {
    CHECK(lba == 2879 && count == 1 && buffer[0] == 0xa5); side_effects++; return 1;
}
int32_t serial_log_read(char* out, uint32_t cap, uint32_t* cursor, uint32_t* lost) {
    (void)cap; (void)lost; out[0] = 'x'; (*cursor)++; side_effects++; return 1;
}
tcp_socket_t* tcp_listen(uint16_t port) { CHECK(port); side_effects++; return (tcp_socket_t*)0x1234; }
void tcp_release(tcp_socket_t* socket) { (void)socket; }
void audio_mixer_close(uint32_t owner) { (void)owner; }
void ipv4_clear_handlers_for_directory(uint32_t owner) { (void)owner; }
void wm_user_close_address_space(uint32_t owner) { (void)owner; }
uint32_t paging_current_directory(void) { return active_directory; }
uint32_t paging_kernel_directory(void) { return 0x1000; }
void paging_activate_directory(uint32_t value) { active_directory = value; }
void paging_activate_kernel(void) { active_directory = paging_kernel_directory(); }
void paging_activate(address_space_t* space) { active_directory = (uint32_t)space->directory; }
int paging_address_space_create(address_space_t* space) {
    for (unsigned i = 0; i < 1024; i++) { directory[i] = 0; table[i] = 0; }
    directory[APP_VIRTUAL_BASE >> 22] = (uint32_t)table | 7;
    space->directory = directory; space->app_page_table = table; return 0;
}
int paging_address_space_alloc_page(address_space_t* space, uint32_t address,
                                    uint8_t writable, uint32_t* physical) {
    (void)space; (void)physical;
    table[(address >> 12)&1023] = address | 5 | (writable ? 2 : 0); return 0;
}
void paging_address_space_destroy(address_space_t* space) {
    space->directory = space->app_page_table = 0; destroyed++;
}
int task_address_space_in_use(uint32_t owner) { (void)owner; return 0; }
int fs_delete_file(const char* path) {
    CHECK(path[0] == 'x' && path[1] == 0); deletes++; return delete_result;
}
int fs_access(const char* path, unsigned access) { (void)path; (void)access; return 1; }
static int read_executable(const char* path, uint8_t* dest, uint32_t capacity) {
    (void)path; CHECK(capacity >= 9);
    app_header_t* header = (app_header_t*)dest;
    *header = (app_header_t){APP_HEADER_MAGIC, executable_type, {0,0,0}};
    dest[8] = 0xc3; /* If a legacy entry executes, this would return. */
    return 9;
}
int task_create(void (*entry)(void*), void* argument, const char* name) {
    (void)name;
    create_calls++;
    child_task.uid = current_task->uid; child_task.gid = current_task->gid;
    queued_entry = entry; queued_argument = argument; return 1;
}
void arch_i386_enter_user(uint32_t entry, uint32_t stack) {
    CHECK(entry == APP_VIRTUAL_BASE + sizeof(app_header_t));
    CHECK(stack >= APP_VIRTUAL_BASE && stack < APP_VIRTUAL_LIMIT);
    CHECK(current_task->uid == expected_uid);
    user_entries++;
}
void task_exit(void) { __builtin_longjmp(exit_context, 1); }

static int32_t call(uint32_t number, uint32_t a, uint32_t b, uint32_t c) {
    registers_t regs = {0}; regs.cs = 0x1b;
    regs.eax = number; regs.ebx = a; regs.ecx = b; regs.edx = c;
    syscall_dispatch(&regs);
    return (int32_t)regs.eax;
}
static void test_dispatch(void) {
    address_space_t space;
    paging_address_space_create(&space);
    for (uint32_t address = APP_VIRTUAL_BASE; address < APP_VIRTUAL_LIMIT; address += PAGE_SIZE)
        paging_address_space_alloc_page(&space, address, 1, 0);
    paging_activate(&space);
    static const uint32_t protected[] = {SYS_SHUTDOWN, SYS_REBOOT, SYS_FLOPPY_INIT,
        SYS_FLOPPY_READ, SYS_FLOPPY_TEST_WRITE, SYS_SERIAL_LOG_READ};
    const uint16_t ordinary[] = {USER_REGULAR, USER_SYSTEM, USER_NOBODY};
    for (unsigned u = 0; u < sizeof(ordinary)/sizeof(ordinary[0]); u++) {
        shell_task.uid = ordinary[u]; shell_task.gid = USER_ROOT;
        for (unsigned i = 0; i < sizeof(protected)/sizeof(protected[0]); i++)
            CHECK(call(protected[i], 0xdeadbeef, 0xdeadbeef, 0xdeadbeef) == SYS_ERR_PERMISSION);
        CHECK(call(SYS_TCP_LISTEN, 21, 0, 0) == SYS_ERR_PERMISSION);
        CHECK(call(SYS_TCP_LISTEN, 1023, 0, 0) == SYS_ERR_PERMISSION);
    }
    CHECK(side_effects == 0);
    CHECK(call(SYS_TCP_LISTEN, 0, 0, 0) == -1);
    CHECK(call(SYS_TCP_LISTEN, 65536, 0, 0) == -1);
    CHECK(call(SYS_TCP_LISTEN, 1024, 0, 0) > 0);
    CHECK(call(SYS_TCP_LISTEN, 8080, 0, 0) > 0);
    CHECK(side_effects == 2);
    *(char*)APP_VIRTUAL_BASE = 'x';
    CHECK(call(SYS_WRITE, APP_VIRTUAL_BASE, 1, 0) == 1 && writes == 1);
    CHECK(call(SYS_FS_DELETE, 0xdeadbeef, 0, 0) == -1 && deletes == 0);
    delete_result = 0;
    CHECK(call(SYS_FS_DELETE, APP_VIRTUAL_BASE, 0, 0) == -1 && deletes == 1);
    delete_result = 1;
    CHECK(call(SYS_FS_DELETE, APP_VIRTUAL_BASE, 0, 0) == 0 && deletes == 2);
    CHECK(call(0xffffffff, 0, 0, 0) == -1);
    shell_task.uid = USER_ROOT;
    CHECK(call(SYS_SHUTDOWN, 0, 0, 0) == -1);
    CHECK(call(SYS_REBOOT, 0, 0, 0) == -1);
    CHECK(call(SYS_FLOPPY_INIT, 0, 0, 0) == 1);
    CHECK(call(SYS_FLOPPY_READ, 0, 1, APP_VIRTUAL_BASE) == 1);
    CHECK(call(SYS_FLOPPY_TEST_WRITE, 0, 0, 0) == 1);
    CHECK(call(SYS_SERIAL_LOG_READ, APP_VIRTUAL_BASE, 1, APP_VIRTUAL_BASE + 512) == 1);
    CHECK(call(SYS_TCP_LISTEN, 21, 0, 0) > 0);
    CHECK(side_effects == 9);
    /* Root authorization does not bypass buffer validation. */
    CHECK(call(SYS_SERIAL_LOG_READ, 0xdeadbeef, 1, 0xdeadbeef) == -1);
    CHECK(call(SYS_FLOPPY_READ, 0, 1, 0xdeadbeef) == 0);
    CHECK(side_effects == 9);
    registers_t regs = {0}; regs.cs = 0x08; regs.eax = SYS_SHUTDOWN;
    syscall_dispatch(&regs); CHECK((int32_t)regs.eax == -1);
    current_task = 0; CHECK(call(SYS_SHUTDOWN, 0, 0, 0) == -1);
    current_task = &shell_task; CHECK(side_effects == 9);
    syscall_forget_address_space((uint32_t)directory);
    print("PASS: dispatcher rejects unauthorized hardware/log/power/low-port calls before side effects; root and ordinary calls work\n");
}
static void test_loader(void) {
    os_api_t api = {0}; api.fs.read = read_executable;
    const uint16_t uids[] = {USER_ROOT, USER_REGULAR};
    for (unsigned u = 0; u < 2; u++) {
        shell_task.uid = uids[u];
        for (executable_type = APP_TYPE_GUI; executable_type <= APP_TYPE_CONSOLE; executable_type++) {
            unsigned before = destroyed;
            CHECK(app_loader_prepare(&api, "/tmp/crafted.bin", "") == -2);
            CHECK(destroyed == before + 1);
            CHECK(user_entries == 0);
        }
    }
    /* A queued sudo launch retains the child's credentials after the shell
     * restores its own UID. Then an ordinary launch must stay ordinary. */
    for (unsigned elevated = 0; elevated < 2; elevated++) {
        shell_task.uid = shell_task.gid = USER_REGULAR;
        if (elevated) CHECK(user_sudo_begin("banana"));
        expected_uid = elevated ? USER_ROOT : USER_REGULAR;
        executable_type = APP_TYPE_USER;
        CHECK(app_loader_launch(&api, "userhello", "test") == 0);
        shell_task.uid = shell_task.gid = USER_REGULAR;
        current_task = &child_task;
        if (!__builtin_setjmp(exit_context)) queued_entry(queued_argument);
        current_task = &shell_task;
        CHECK(shell_task.uid == USER_REGULAR);
        CHECK(child_task.uid == expected_uid);
    }
    CHECK(user_entries == 2);
    print("PASS: loader rejects both ring-0 formats even as root; ordinary and sudo children use the user entry path\n");
}
static Window* registered_dialog;
static unsigned draw_calls;
static int register_dialog(Window* window) {
    registered_dialog = window;
    window->visible = window->focused = 1;
    return 0;
}
static void unregister_dialog(Window* window) {
    CHECK(window == registered_dialog); registered_dialog = 0;
}
static void test_fill(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    (void)x; (void)y; (void)w; (void)h; (void)color; draw_calls++;
}
static void test_text(int32_t x, int32_t y, const char* text, uint32_t color) {
    (void)x; (void)y; (void)text; (void)color; draw_calls++;
}
static void test_button(int32_t x, int32_t y, int32_t w, int32_t h, const char* text, uint8_t pressed) {
    (void)x; (void)y; (void)w; (void)h; (void)text; (void)pressed; draw_calls++;
}
void request_redraw(void) { }
static void test_builtin_shutdown(void) {
    os_api_t api = {0};
    api.windows.register_window = register_dialog;
    api.windows.unregister_window = unregister_dialog;
    api.graphics.fill_rect = test_fill;
    api.graphics.draw_string = test_text;
    api.graphics.draw_button = test_button;
    unsigned created = create_calls, shut = shutdown_calls, reboot = reboot_calls;
    shell_task.uid = shell_task.gid = USER_REGULAR;
    shutdown_dialog_init(&api);
    CHECK(shutdown_dialog_open() == 0 && registered_dialog);
    registered_dialog->draw_content(registered_dialog);
    CHECK(draw_calls > 0);
    registered_dialog->handle_key(registered_dialog, 27);
    CHECK(!registered_dialog && shutdown_calls == shut && reboot_calls == reboot);
    CHECK(shutdown_dialog_open() == 0);
    registered_dialog->handle_mouse(registered_dialog, 210, 135, 1); /* Cancel */
    CHECK(!registered_dialog && shutdown_calls == shut && reboot_calls == reboot);
    CHECK(shutdown_dialog_open() == 0);
    registered_dialog->handle_key(registered_dialog, '\n');
    CHECK(shutdown_calls == shut + 1 && reboot_calls == reboot);
    registered_dialog->handle_key(registered_dialog, 0x50);
    registered_dialog->handle_key(registered_dialog, '\n');
    CHECK(shutdown_calls == shut + 1 && reboot_calls == reboot + 1);
    CHECK(shutdown_dialog_open() == 0); /* Resets selection to shutdown. */
    registered_dialog->handle_mouse(registered_dialog, 70, 135, 1);
    CHECK(shutdown_calls == shut + 2 && reboot_calls == reboot + 1);
    CHECK(create_calls == created); /* No executable/process is launched. */
    CHECK(shell_task.uid == USER_REGULAR && shell_task.gid == USER_REGULAR);
    CHECK(call(SYS_SHUTDOWN, 0, 0, 0) == SYS_ERR_PERMISSION);
    CHECK(call(SYS_REBOOT, 0, 0, 0) == SYS_ERR_PERMISSION);
    print("PASS: built-in shutdown dialog renders, cancels, powers off and restarts without launching or elevating an app\n");
}
void _start(void) {
    uint32_t args[] = {APP_VIRTUAL_BASE, APP_VIRTUAL_LIMIT - APP_VIRTUAL_BASE, 3, 0x32, 0xffffffff, 0};
    uint32_t mapped;
    __asm__ volatile("int $0x80" : "=a"(mapped) : "0"(90), "b"(args) : "memory");
    CHECK(mapped == APP_VIRTUAL_BASE);
    test_dispatch(); test_loader(); test_builtin_shutdown(); finish(0);
}
