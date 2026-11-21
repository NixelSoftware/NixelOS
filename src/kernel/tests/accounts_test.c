/* Native account lifecycle tests with a permission-aware in-memory filesystem. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../users.c"
task_t* current_task;
void mutex_lock(mutex_t* m) { (void)m; }
void mutex_unlock(mutex_t* m) { (void)m; }
static unsigned entropy_counter;
static int fail_entropy, fail_write;
int kernel_entropy(void* c,uint8_t* out,unsigned n) {
    (void)c; if(fail_entropy) return -1;
    while(n--) *out++=++entropy_counter; return 0;
}
static struct file { char path[96]; fs_stat_t st; int dir, size; uint8_t data[4096]; } files[128];
static unsigned count;
static struct file* file(const char* path) {
    for(unsigned i=0;i<count;i++) if(!strcmp(files[i].path,path)) return &files[i];
    return 0;
}
int fs_stat(const char* p,fs_stat_t* st) { struct file* f=file(p); if(!f) return 0; *st=f->st; return 1; }
int fs_is_directory(const char* p) { struct file* f=file(p); return f && f->dir; }
int fs_mkdir(const char* p) {
    struct file* f=file(p); if(f) return f->dir ? 0 : -1;
    f=&files[count++]; strcpy(f->path,p); f->dir=1; f->st.mode=0755; return 0;
}
int fs_chown(const char* p,uint16_t uid) { struct file* f=file(p); if(!f || !user_by_id(uid)) return 0; f->st.uid=uid; return 1; }
int fs_chmod(const char* p,uint16_t mode) { struct file* f=file(p); if(!f) return 0; f->st.mode=mode; return 1; }
int fs_write_file(const char* p,const uint8_t* data,uint32_t size) {
    if(fail_write || size>4096) return -1;
    struct file* f=file(p); if(!f) { f=&files[count++]; strcpy(f->path,p); f->st.mode=0644; }
    if(size) memcpy(f->data,data,size); f->size=size; return size;
}
int fs_read_file(const char* p,uint8_t* data,uint32_t cap) {
    struct file* f=file(p); if(!f) return -1;
    if(!data || !cap) return f->size;
    if(cap>(unsigned)f->size) cap=f->size;
    memcpy(data,f->data,cap); return cap;
}
static void reboot(void) { initialized=0; memset(users,0,sizeof(users)); }
int main(void) {
    uint8_t salt[16], result[32];
    for(unsigned i=0;i<16;i++) salt[i]=i;
    const uint8_t expected[32]={0x3b,0xc3,0x71,0x18,0xe6,0x25,0x09,0x3e,0x9b,0x79,0xed,0x08,0x93,0x0e,0xa7,0xaf,0x73,0x89,0x59,0x12,0x33,0xfd,0xd9,0x2d,0xdd,0xf3,0x69,0x37,0x1e,0x60,0xdb,0xc0};
    assert(derive("password",salt,result)); assert(!memcmp(result,expected,32));
    assert(users_init());
    assert(user_authenticate("root","banana")); assert(user_authenticate("user","user"));
    assert(!user_authenticate("root","wrong")); assert(!user_authenticate("system",""));
    assert(memcmp(users[0].salt,users[1].salt,16));
    assert(file(ACCOUNT_PATH)->st.mode==0600); assert(file("/etc/accounts")->st.mode==0700);
    assert(user_create("alice","secret")); assert(user_by_name("alice")->uid==1001);
    assert(file("/home/alice/Documents")->st.uid==1001);
    assert(!user_create("alice","other")); assert(!user_create("../escape","secret"));
    assert(!user_create("bob",""));
    assert(user_set_password("alice","changed")); assert(!user_authenticate("alice","secret"));
    assert(user_set_locked("alice",1)); assert(!user_authenticate("alice","changed"));
    assert(!user_set_locked("root",1)); assert(!user_set_password("system","x"));
    reboot(); assert(users_init()); assert(!user_authenticate("alice","changed"));
    assert(user_set_locked("alice",0)); assert(user_authenticate("alice","changed"));
    fail_entropy=1; assert(!user_set_password("alice","lost")); fail_entropy=0;
    assert(user_authenticate("alice","changed"));
    fail_write=1; assert(!user_set_password("alice","lost")); fail_write=0;
    assert(user_authenticate("alice","changed"));
    task_t task={.uid=1000,.gid=1000}; current_task=&task;
    assert(!user_create("eve","secret")); assert(!user_set_password("root","secret"));
    assert(!user_set_locked("alice",1)); assert(!strcmp(user_home(),"/home/user"));
    assert(user_sudo_begin("banana")); assert(user_switch("alice"));
    assert(!strcmp(user_home(),"/home/alice")); current_task=0;
    reboot(); file(ACCOUNT_PATH)->data[0]=99; assert(!users_init());
    assert(!user_authenticate("root","banana"));
    reboot(); memset(files,0,sizeof(files)); count=0; fail_entropy=1;
    assert(!users_init()); assert(!user_can_logon(user_by_name("root")));
    fail_entropy=0; assert(users_init());
    reboot(); file(ACCOUNT_PATH)->size=0; assert(!users_init());
    assert(!user_authenticate("root","banana"));
    puts("Account lifecycle, persistence, permissions, and failure tests passed.");
    return 0;
}
