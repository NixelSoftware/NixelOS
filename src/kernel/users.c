#include "users.h"
#include "task.h"
const user_t users[4] = {
    {USER_ROOT, USER_ROOT, "root"}, {USER_REGULAR, USER_REGULAR, "user"},
    {USER_SYSTEM, USER_SYSTEM, "system"}, {USER_NOBODY, USER_NOBODY, "nobody"}
};
const user_t* user_by_name(const char* name) {
    if (!name) return 0;
    for (int i = 0; i < 4; i++) {
        int j = 0;
        while (name[j] && name[j] == users[i].name[j]) j++;
        if (!name[j] && !users[i].name[j]) return &users[i];
    }
    return 0;
}
const user_t* user_by_id(uint16_t uid) {
    for (int i = 0; i < 4; i++) if (users[i].uid == uid) return &users[i];
    return 0;
}
uint16_t user_uid(void) { return current_task ? current_task->uid : USER_ROOT; }
uint16_t user_gid(void) { return current_task ? current_task->gid : USER_ROOT; }
int user_switch(const char* name) {
    const user_t* user = user_by_name(name);
    if (!current_task || user_uid() != USER_ROOT || !user) return 0;
    current_task->uid = user->uid;
    current_task->gid = user->gid;
    return 1;
}
