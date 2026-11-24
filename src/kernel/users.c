/* Versioned development account database. Never store plaintext credentials. */
#include "users.h"
#include "task.h"
#include "fs.h"
#include "mutex.h"
#include "crypto/entropy.h"
#include <wolfssl/wolfcrypt/pwdbased.h>
#include <wolfssl/wolfcrypt/sha256.h>
#define ACCOUNT_PATH "/etc/accounts/database"
#define PASSWORD_ROUNDS 600000
user_t users[USER_COUNT];
static mutex_t accounts_mutex;
static struct { uint32_t version; user_t records[USER_COUNT]; } disk;
static int initialized;
static void erase(void* p, unsigned n) { volatile uint8_t* b=p; while(n--) *b++=0; }
static unsigned length(const char* s) { unsigned n=0; if(s) while(s[n]) n++; return n; }
static int equal(const char* a,const char* b) {
    if(!a || !b) return 0;
    while(*a && *a==*b) { a++; b++; } return *a==*b;
}
static void copy(char* d,const char* s) { while((*d++=*s++)) {} }
static int valid_name(const char* s) {
    unsigned n=length(s); if(!n || n>31) return 0;
    for(unsigned i=0;i<n;i++) if(!((s[i]>='a'&&s[i]<='z') ||
        (i && s[i]>='0'&&s[i]<='9') || s[i]=='_')) return 0;
    return 1;
}
const user_t* user_by_name(const char* name) {
    for(unsigned i=0;i<USER_COUNT;i++) if(users[i].name[0] && equal(name,users[i].name)) return &users[i];
    return 0;
}
const user_t* user_by_id(uint16_t uid) {
    for(unsigned i=0;i<USER_COUNT;i++) if(users[i].name[0] && users[i].uid==uid) return &users[i];
    return 0;
}
uint16_t user_uid(void) { return current_task ? current_task->uid : USER_ROOT; }
const char* user_home(void) {
    const user_t* u=user_by_id(user_uid()); return u ? u->home : "/tmp";
}
int user_can_logon(const user_t* u) { return u && u->name[0] && u->enabled; }
static int derive(const char* password,const uint8_t salt[16],uint8_t hash[32]) {
    unsigned n=length(password);
    return n && n<=USER_PASSWORD_MAX &&
        wc_PBKDF2(hash,(const uint8_t*)password,n,salt,16,PASSWORD_ROUNDS,32,WC_SHA256)==0;
}
int user_authenticate(const char* name,const char* password) {
    MUTEX_GUARD(accounts_mutex);
    const user_t* u=user_by_name(name); uint8_t hash[32]={0}; unsigned difference=0;
    if(!user_can_logon(u) || !derive(password,u->salt,hash)) { erase(hash,32); return 0; }
    for(unsigned i=0;i<32;i++) difference |= hash[i]^u->hash[i];
    erase(hash,32); return difference==0;
}
static int password_record(user_t* u,const char* password) {
    return kernel_entropy(0,u->salt,16)==0 && derive(password,u->salt,u->hash);
}
static int save(void) {
    disk.version=1;
    for(unsigned i=0;i<USER_COUNT;i++) disk.records[i]=users[i];
    int ok=fs_write_file(ACCOUNT_PATH,(const uint8_t*)&disk,sizeof(disk))==(int)sizeof(disk);
    erase(&disk,sizeof(disk)); return ok;
}
static int home_create(user_t* u) {
    static const char* dirs[]={"Desktop","Documents","Downloads","Pictures","Music",".config"};
    char path[96];
    if(fs_mkdir(u->home)<0 || !fs_chown(u->home,u->uid) || !fs_chmod(u->home,0700)) return 0;
    for(unsigned i=0;i<sizeof(dirs)/sizeof(dirs[0]);i++) {
        copy(path,u->home); unsigned n=length(path); path[n++]='/'; copy(path+n,dirs[i]);
        if(fs_mkdir(path)<0 || !fs_chown(path,u->uid) || !fs_chmod(path,0700)) return 0;
    }
    return 1;
}
int users_init(void) {
    MUTEX_GUARD(accounts_mutex);
    if(initialized) return 1;
    fs_stat_t st;
    if(fs_stat("/etc/accounts",&st)) {
        if(st.uid!=0 || (st.mode&0777)!=0700 || !fs_is_directory("/etc/accounts")) return 0;
        /* Existing but missing/corrupt database is an error, never reseed credentials. */
        if(!fs_stat(ACCOUNT_PATH,&st) || st.uid!=0 || (st.mode&0777)!=0600 ||
            fs_read_file(ACCOUNT_PATH,0,0)!=(int)sizeof(disk) ||
            fs_read_file(ACCOUNT_PATH,(uint8_t*)&disk,sizeof(disk))!=(int)sizeof(disk) || disk.version!=1) return 0;
        for(unsigned i=0;i<USER_COUNT;i++) {
            user_t* u=&disk.records[i]; if(!u->name[0]) continue;
            if(u->name[31] || u->home[63] || !valid_name(u->name) || u->home[0]!='/' || u->enabled>1) goto invalid;
            for(unsigned j=0;j<i;j++) if(disk.records[j].name[0] &&
                (disk.records[j].uid==u->uid || equal(disk.records[j].name,u->name))) goto invalid;
        }
        for(unsigned i=0;i<USER_COUNT;i++) users[i]=disk.records[i];
        erase(&disk,sizeof(disk));
        if(!user_can_logon(user_by_name("root")) || user_by_name("root")->uid!=0 ||
            !user_by_id(USER_SYSTEM) || !user_by_id(USER_NOBODY)) { erase(users,sizeof(users)); return 0; }
        initialized=1; return 1;
    }
    /* Temporary first-boot fixtures; only hashes are persisted. */
    users[0]=(user_t){.uid=0,.gid=0,.name="root",.home="/root",.enabled=1};
    users[1]=(user_t){.uid=1000,.gid=1000,.name="user",.home="/home/user",.enabled=1};
    users[2]=(user_t){.uid=1,.gid=1,.name="system",.home="/var/lib/nixel"};
    users[3]=(user_t){.uid=65534,.gid=65534,.name="nobody",.home="/tmp"};
    if(!password_record(&users[0],"banana") || !password_record(&users[1],"user") ||
        !home_create(&users[0]) || !home_create(&users[1]) ||
        fs_mkdir("/etc/accounts")<0 || !fs_chmod("/etc/accounts",0700) ||
        fs_write_file(ACCOUNT_PATH,0,0)!=0 || !fs_chmod(ACCOUNT_PATH,0600) || !save()) goto invalid;
    initialized=1; return 1;
invalid:
    erase(&disk,sizeof(disk)); erase(users,sizeof(users)); return 0;
}
int user_create(const char* name,const char* password) {
    MUTEX_GUARD(accounts_mutex);
    if(!initialized || user_uid()!=0 || !valid_name(name) || user_by_name(name)) return 0;
    unsigned slot=USER_COUNT; uint16_t uid=1000;
    for(unsigned i=0;i<USER_COUNT;i++) if(!users[i].name[0]) { slot=i; break; }
    if(slot==USER_COUNT) return 0;
    while(user_by_id(uid) && uid<65533) uid++;
    if(user_by_id(uid)) return 0;
    user_t u={.uid=uid,.gid=uid,.enabled=1}; copy(u.name,name); copy(u.home,"/home/"); copy(u.home+6,name);
    if(fs_is_directory(u.home) || !password_record(&u,password)) { erase(&u,sizeof(u)); return 0; }
    users[slot]=u; erase(&u,sizeof(u));
    if(home_create(&users[slot]) && save()) return 1;
    erase(&users[slot],sizeof(user_t)); return 0;
}
int user_set_password(const char* name,const char* password) {
    MUTEX_GUARD(accounts_mutex);
    user_t* u=(user_t*)user_by_name(name);
    if(!initialized || user_uid()!=0 || !u || u->uid==1 || u->uid==65534) return 0;
    user_t old=*u;
    int ok=password_record(u,password) && save(); if(!ok) *u=old;
    erase(&old,sizeof(old)); return ok;
}
int user_set_locked(const char* name,int locked) {
    MUTEX_GUARD(accounts_mutex);
    user_t* u=(user_t*)user_by_name(name);
    if(!initialized || user_uid()!=0 || !u || u->uid==0 || u->uid==1 || u->uid==65534) return 0;
    uint8_t old=u->enabled; u->enabled=!locked;
    if(save()) return 1;
    u->enabled=old; return 0;
}
uint16_t user_gid(void) { return current_task ? current_task->gid : USER_ROOT; }
int user_switch(const char* name) {
    const user_t* user = user_by_name(name);
    if (!current_task || user_uid() != USER_ROOT || !user_can_logon(user)) return 0;
    current_task->uid = user->uid;
    current_task->gid = user->gid;
    current_task->cwd[0] = 0;
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
