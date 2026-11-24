#ifndef USERS_H
#define USERS_H
#include "types.h"
#define USER_ROOT 0
#define USER_SYSTEM 1
#define USER_REGULAR 1000
#define USER_NOBODY 65534
#define USER_COUNT 16
#define USER_PASSWORD_MAX 63
typedef struct {
    uint16_t uid, gid;
    char name[32];
    char home[64];
    uint8_t enabled;
    uint8_t salt[16], hash[32];
} user_t;
extern user_t users[USER_COUNT];
int users_init(void);
int user_create(const char* name, const char* password);
int user_set_password(const char* name, const char* password);
int user_set_locked(const char* name, int locked);
const user_t* user_by_name(const char* name);
const user_t* user_by_id(uint16_t uid);
int user_authenticate(const char* name, const char* password);
int user_can_logon(const user_t* user);
const char* user_home(void);
uint16_t user_uid(void);
uint16_t user_gid(void);
int user_sudo_begin(const char* password);
int user_switch(const char* name);
#endif
