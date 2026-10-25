#include "users.h"
#include "task.h"
const user_t users[USER_COUNT] = {
    {USER_ROOT, USER_ROOT, "root", "banana"},
    {USER_REGULAR, USER_REGULAR, "user", "user"},
    {USER_SYSTEM, USER_SYSTEM, "system", 0},
    {USER_NOBODY, USER_NOBODY, "nobody", 0}
};

static int text_equal(const char* left, const char* right) {
    unsigned difference = 0;
    unsigned i = 0;
    if (!left || !right) return 0;
    while (left[i] || right[i]) {
        difference |= (unsigned char)left[i] ^ (unsigned char)right[i];
        i++;
    }
    return difference == 0;
}

const user_t* user_by_name(const char* name) {
    if (!name) return 0;
    for (int i = 0; i < USER_COUNT; i++) {
        int j = 0;
        while (name[j] && name[j] == users[i].name[j]) j++;
        if (!name[j] && !users[i].name[j]) return &users[i];
    }
    return 0;
}
int user_can_logon(const user_t* user) {
    return user && user->password != 0;
}
int user_authenticate(const char* name, const char* password) {
    const user_t* user = user_by_name(name);
    return user_can_logon(user) && password &&
           text_equal(password, user->password);
}
const user_t* user_by_id(uint16_t uid) {
    for (int i = 0; i < USER_COUNT; i++) if (users[i].uid == uid) return &users[i];
    return 0;
}
uint16_t user_uid(void) { return current_task ? current_task->uid : USER_ROOT; }
uint16_t user_gid(void) { return current_task ? current_task->gid : USER_ROOT; }
int user_switch(const char* name) {
    const user_t* user = user_by_name(name);
    if (!current_task || user_uid() != USER_ROOT || !user_can_logon(user)) return 0;
    current_task->uid = user->uid;
    current_task->gid = user->gid;
    return 1;
}

/* Authenticate the root account for sudo, then change only this shell task. */
int user_sudo_begin(const char* password) {
    if (!current_task || !password || !user_can_logon(user_by_id(user_uid())) ||
        !user_authenticate("root", password)) return 0;
    current_task->uid = USER_ROOT;
    current_task->gid = USER_ROOT;
    return 1;
}
