#ifndef USERS_H
#define USERS_H
#include "types.h"
#define USER_ROOT 0
#define USER_SYSTEM 1
#define USER_REGULAR 1000
#define USER_NOBODY 65534
typedef struct { uint16_t uid, gid; const char* name; } user_t;
extern const user_t users[4];
const user_t* user_by_name(const char* name);
const user_t* user_by_id(uint16_t uid);
uint16_t user_uid(void);
uint16_t user_gid(void);
int user_switch(const char* name);
#endif
