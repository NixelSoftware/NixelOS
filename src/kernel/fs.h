#ifndef FS_H
#define FS_H

#include "types.h"

#define FS_MAX_FILENAME      32
#define FS_MAX_FILES         128
#define FS_MAX_PATH          256


/* Directory type bit retained by the filesystem syscall ABI. */
#define FS_ATTR_DIRECTORY 0x10

#define FS_ACCESS_READ 4
#define FS_ACCESS_WRITE 2
#define FS_ACCESS_EXEC 1
typedef struct { uint16_t uid, gid, mode; } fs_stat_t;
int fs_stat(const char* path, fs_stat_t* out);
int fs_access(const char* path, unsigned access);
int fs_chmod(const char* path, uint16_t mode);
int fs_chown(const char* path, uint16_t uid);

int fs_read_file_at(const char* filename, uint8_t* dest, uint32_t capacity, uint32_t offset);
int fs_list_dir_types(const char* path, char names[FS_MAX_FILES][FS_MAX_FILENAME], uint8_t types[FS_MAX_FILES]);
uint32_t fs_generation(void);
const char* fs_getcwd(void);
int fs_chdir(const char* path);
int fs_home_path(const char* suffix, char out[FS_MAX_PATH]);
int fs_mkdir(const char* path);
int fs_is_directory(const char* path);
void fs_init(void);
int  fs_list(char file_list_out[FS_MAX_FILES][FS_MAX_FILENAME]);
int  fs_read_file(const char* filename, uint8_t* dest, uint32_t max_bytes);
int  fs_write_file(const char* filename, const uint8_t* src, uint32_t size_bytes);
int  fs_delete_file(const char* filename);


int  fs_list_dir(const char* path, char file_list_out[FS_MAX_FILES][FS_MAX_FILENAME]);

#endif