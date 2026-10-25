#ifndef USERS_H
#define USERS_H
#include "types.h"
#define USER_ROOT 0
#define USER_SYSTEM 1
#define USER_REGULAR 1000
#define USER_NOBODY 65534
#define USER_COUNT 4
typedef struct {
    uint16_t uid, gid;
    const char* name;
    const char* password;
} user_t;
extern const user_t users[USER_COUNT];
const user_t* user_by_name(const char* name);
const user_t* user_by_id(uint16_t uid);
int user_authenticate(const char* name, const char* password);
int user_can_logon(const user_t* user);
uint16_t user_uid(void);
uint16_t user_gid(void);
int user_sudo_begin(const char* password);
int user_switch(const char* name);
#endif
