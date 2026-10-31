/* Host integration harness: actual kernel backend, memory-backed ATA sectors. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define int64_t kernel_int64_t
#define uint64_t kernel_uint64_t
#include "../fs.c"
#undef int64_t
#undef uint64_t

mutex_t filesystem_mutex;
void mutex_lock(mutex_t* m) { (void)m; }
void mutex_unlock(mutex_t* m) { (void)m; }
void debug_print(const char* text) { fputs(text,stdout); }
void debug_hex(uint32_t n) { (void)n; }
task_t* current_task;
static task_t test_task;
static uint16_t uid=0;
const char* user_home(void) { return uid ? "/home/user" : "/root"; }
uint16_t user_uid(void) { return uid; }
uint16_t user_gid(void) { return uid; }
const user_t* user_by_id(uint16_t n) {
    static user_t u; if (n!=0 && n!=1000) return 0;
    u.uid=u.gid=n; return &u;
}
static uint8_t* disk;
static size_t disk_size;
static unsigned selected, duplicate, fail_write;
int ata_select_drive(unsigned d) { selected=d; return d==0 || (duplicate && d==1); }
int ata_read_sectors(uint32_t lba, uint8_t n, uint8_t* out) {
    if ((selected && !(duplicate && selected==1)) || (size_t)lba*512+(size_t)n*512>disk_size) return 0;
    memcpy(out,disk+(size_t)lba*512,n*512); return 1;
}
int ata_write_sectors(uint32_t lba, uint8_t n, const uint8_t* data) {
    if (fail_write && lba!=base+2) { fail_write=0; return 0; }
    if (selected || (size_t)lba*512+(size_t)n*512>disk_size) return 0;
    memcpy(disk+(size_t)lba*512,data,n*512); return 1;
}
#define CHECK(expr) do { if (!(expr)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#expr); exit(1); } } while (0)
static uint8_t data[2*1024*1024+13], out[sizeof(data)];
static char names[FS_MAX_FILES][FS_MAX_FILENAME];
int main(int argc, char** argv) {
    CHECK(argc==2);
    FILE* f=fopen(argv[1],"rb"); CHECK(f);
    fseek(f,0,SEEK_END); disk_size=ftell(f); rewind(f);
    disk=malloc(disk_size); CHECK(disk); CHECK(fread(disk,1,disk_size,f)==disk_size); fclose(f);
    uint8_t boot[512]; memcpy(boot,disk,512);
    mount_boot_disk(boot); CHECK(ready);
    CHECK(fs_is_directory("/"));
    CHECK(fs_is_directory("/usr/bin") && fs_is_directory("/etc") && fs_is_directory("/var/log"));
    CHECK(fs_is_directory("/root") && fs_is_directory("/home/user/Documents")); CHECK(fs_is_directory("/home/user/Desktop"));
    fs_stat_t st; CHECK(fs_stat("/home/user/Desktop",&st)); CHECK(st.uid==1000 && st.mode==0755);
    CHECK(fs_read_file("/home/user/seed.bin",out,sizeof(out))==10000);
    for (unsigned i=0;i<10000;i++) CHECK(out[i]==(uint8_t)(i*17));
    CHECK(fs_read_file_at("/home/user/SEED.BIN",out,531,4090)==531);
    for (unsigned i=0;i<531;i++) CHECK(out[i]==(uint8_t)((i+4090)*17));
    CHECK(fs_read_file_at("/home/user/seed.bin",out,100,10000)==0);
    CHECK(fs_read_file("missing",out,10)==-1);
    CHECK(fs_write_file("empty",0,0)==0); CHECK(fs_read_file("empty",0,0)==0);
    CHECK(fs_delete_file("empty"));
    for (unsigned i=0;i<sizeof(data);i++) data[i]=i*31;
    uint32_t free_before=u32(super+12), ino_before=u32(super+16);
    CHECK(fs_write_file("large-long-filename.bin",data,sizeof(data))==sizeof(data));
    CHECK(fs_read_file("large-long-filename.bin",out,sizeof(out))==sizeof(data));
    CHECK(!memcmp(data,out,sizeof(data)));
    CHECK(fs_write_file("large-long-filename.bin",data,17)==17);
    CHECK(fs_read_file("large-long-filename.bin",out,sizeof(out))==17);
    CHECK(!memcmp(data,out,17));
    CHECK(fs_write_file("large-long-filename.bin",0,0)==0);
    CHECK(fs_delete_file("large-long-filename.bin"));
    CHECK(u32(super+12)==free_before && u32(super+16)==ino_before);
    CHECK(fs_mkdir("/home/user/Desktop/subdir")==0); CHECK(fs_mkdir("/home/user/Desktop/subdir")==0);
    current_task=&test_task;
    uid=1000;
    CHECK(!strcmp(fs_getcwd(), "/home/user"));
    CHECK(fs_chdir("Desktop")); CHECK(!strcmp(fs_getcwd(), "/home/user/Desktop"));
    CHECK(fs_chdir("../Documents")); CHECK(!strcmp(fs_getcwd(), "/home/user/Documents"));
    CHECK(fs_write_file("relative-long-file.txt",data,27)==27);
    CHECK(fs_read_file("/home/user/Documents/relative-long-file.txt",out,27)==27);
    CHECK(!fs_chdir("/root")); CHECK(!fs_chdir("/home/user/../user/../../root"));
    CHECK(fs_write_file("/etc/denied",data,1)==-1);
    CHECK(fs_chdir("/home/user"));
    CHECK(fs_write_file("/home/user/Desktop/user-note.txt",data,5100)==5100);
    CHECK(fs_write_file("/home/user/Desktop/subdir/forbidden",data,1)==-1);
    CHECK(fs_chown("/home/user/seed.bin",1000)==0);
    CHECK(fs_chmod("/home/user/seed.bin",0777)==0);
    CHECK(fs_write_file("/home/user/seed.bin",data,1)==-1);
    CHECK(fs_delete_file("/usr/bin/calc.bin")==0);
    CHECK(fs_delete_file("/home/user/seed.bin"));
    CHECK(fs_read_file("/usr/bin/calc.bin",out,sizeof(out))==10000);
    CHECK(fs_mkdir("/home/user/Desktop/own-dir")==0);
    CHECK(fs_write_file("/home/user/Desktop/own-dir/note.txt",data,4111)==4111);
    CHECK(fs_read_file("/home/user/Desktop/own-dir/../user-note.txt",out,5100)==5100);
    CHECK(fs_chmod("/home/user/Desktop/own-dir",0000));
    CHECK(fs_read_file("/home/user/Desktop/own-dir/note.txt",out,1)==-1);
    CHECK(fs_chmod("/home/user/Desktop/own-dir",0755));
    CHECK(fs_delete_file("/home/user/Desktop/own-dir/note.txt"));
    CHECK(fs_delete_file("/home/user/Desktop/own-dir")==0);
    CHECK(fs_mkdir("/home/user/Desktop/../.")==-1);
    CHECK(fs_write_file("/home/user/Desktop/..",data,1)==-1);
    /* Grow a directory beyond a block, then reuse deleted directory records. */
    for (unsigned i=0;i<125;i++) {
        char path[100]; snprintf(path,sizeof(path),"/home/user/Desktop/own-dir/file-number-%03u.txt",i);
        CHECK(fs_write_file(path,data,23)==23);
    }
    CHECK(fs_list_dir("/home/user/Desktop/own-dir",names)==125);
    for (unsigned i=0;i<125;i++) {
        char path[100]; snprintf(path,sizeof(path),"/home/user/Desktop/own-dir/file-number-%03u.txt",i);
        CHECK(fs_delete_file(path));
    }
    CHECK(fs_list_dir("/home/user/Desktop/own-dir",names)==0);
    CHECK(fs_write_file("/home/user/Desktop/own-dir/retained.txt",data,8193)==8193);
    /* ENOSPC preserves old contents. */
    CHECK(fs_write_file("/home/user/Desktop/own-dir/retained.txt",data,32768u*4096)==-1);
    CHECK(fs_read_file("/home/user/Desktop/own-dir/retained.txt",out,sizeof(out))==8193);
    CHECK(!memcmp(data,out,8193));
    uid=0;
    CHECK(fs_chown("/home/user/Desktop/subdir",1000));
    CHECK(fs_stat("/home/user/Desktop/subdir",&st) && st.uid==1000 && st.gid==1000);
    mount_boot_disk(boot); CHECK(ready);
    CHECK(fs_read_file("/home/user/Desktop/own-dir/retained.txt",out,sizeof(out))==8193);
    /* Reject an unsupported feature / dirty filesystem / duplicated boot disk. */
    size_t sb=(size_t)base*512+1024;
    uint8_t save=disk[sb+96]; disk[sb+96]|=0x80;
    mount_boot_disk(boot); CHECK(!ready); disk[sb+96]=save;
    disk[sb+58]=0; mount_boot_disk(boot); CHECK(!ready); disk[sb+58]=1;
    duplicate=1; mount_boot_disk(boot); CHECK(!ready); duplicate=0;
    mount_boot_disk(boot); CHECK(ready);
    uint8_t* snapshot=malloc(disk_size); CHECK(snapshot); memcpy(snapshot,disk,disk_size);
    fail_write=1;
    CHECK(fs_write_file("/tmp/io-error.txt",data,10)==-1);
    CHECK(u16(super+58)==0);
    CHECK(fs_write_file("/tmp/after-error.txt",data,10)==-1);
    mount_boot_disk(boot); CHECK(!ready);
    memcpy(disk,snapshot,disk_size); free(snapshot);
    mount_boot_disk(boot); CHECK(ready);
    f=fopen(argv[1],"wb"); CHECK(f); CHECK(fwrite(disk,1,disk_size,f)==disk_size); fclose(f);
    free(disk); puts("PASS ext4 kernel read/write/permissions/remount"); return 0;
}
