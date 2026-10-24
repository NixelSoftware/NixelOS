/* Host fixture drives password keystrokes while the shell sleeps. */
#include "../../src/system/graphics/terminal/terminal.c"
#include "../../src/kernel/users.c"
static task_t actor;
task_t* current_task = &actor;
mutex_t kernel_mutex;
static const char* keys;
void mutex_lock(mutex_t* lock) { (void)lock; }
void mutex_unlock(mutex_t* lock) { (void)lock; }
void wm_invalidate(void) { }
int shell_interrupt(terminal_t* t) { (void)t; return 1; }
int shell_submit_command(terminal_t* t, const char* c, shell_output_fn o, shell_clear_fn clear) {
    (void)t; (void)c; (void)o; (void)clear; return 1;
}
void task_sleep_ms(uint32_t ms) {
    (void)ms;
    if (*keys) terminal_handle_key(&terminals[0].window, *keys++);
    else actor.terminate_requested = 1;
}
#define CHECK(c) do { if (!(c)) return __LINE__; } while (0)
int main(void) {
    actor.uid = actor.gid = USER_REGULAR;
    CHECK(!user_sudo_begin(0));
    CHECK(!user_sudo_begin(""));
    CHECK(!user_sudo_begin("banan"));
    CHECK(!user_sudo_begin("bananas"));
    CHECK(!user_sudo_begin("Banana"));
    CHECK(actor.uid == USER_REGULAR);
    CHECK(user_sudo_begin("banana"));
    CHECK(actor.uid == USER_ROOT && actor.gid == USER_ROOT);
    actor.uid = USER_NOBODY;
    CHECK(!user_sudo_begin("banana"));
    actor.uid = USER_SYSTEM;
    CHECK(!user_sudo_begin("banana"));
    terminal_t* t = &terminals[0];
    t->active = 1; actor.terminal = t;
    char password[64];
    keys = "bananx\ba\n";
    CHECK(terminal_read_password(password, sizeof(password)));
    CHECK(password[0] == 'b' && password[5] == 'a' && !password[6]);
    CHECK(t->term_lines[0][0] == 0); /* No password echo. */
    for (unsigned i = 0; i < sizeof(t->password); i++) CHECK(!t->password[i]);
    keys = "banana\003";
    CHECK(!terminal_read_password(password, sizeof(password)));
    CHECK(!t->password_pending);
    keys = "";
    CHECK(!terminal_read_password(password, sizeof(password)));
    CHECK(!t->password_pending);
    return 0;
}
