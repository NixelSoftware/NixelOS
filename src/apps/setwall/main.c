#include "../../system/user/sys_api.h"
#include "../../kernel/types.h"

#define CHUNK_SIZE 16384u

static uint8_t chunk_buffer[CHUNK_SIZE];

static void print(const char* text) {
    uint32_t length = 0;
    while (text[length]) length++;
    (void)sys_write(text, length);
}

void user_main(const char* argument) {
    char source_path[FS_MAX_PATH];
    uint32_t n = 0;

    while (argument && *argument == ' ') argument++;

    if (!argument || !*argument) {
        print("Usage: setwall <file.bmp>\n");
        sys_exit(1);
    }

    while (argument[n] && argument[n] != ' ' && argument[n] != '\n' && n < sizeof(source_path) - 1u) {
        source_path[n] = argument[n];
        n++;
    }
    source_path[n] = '\0';

    int32_t total_size = sys_fs_read(source_path, 0, 0);
    if (total_size <= 54) {
        print("setwall: file not found or invalid size\n");
        sys_exit(1);
    }

    if (sys_fs_read_at(source_path, chunk_buffer, 2, 0) != 2 ||
        chunk_buffer[0] != 'B' || chunk_buffer[1] != 'M') {
        print("setwall: file is not a valid BMP image\n");
        sys_exit(1);
    }

    if ((uint32_t)total_size > sizeof(chunk_buffer)) {
        print("setwall: image exceeds maximum transfer buffer\n");
        sys_exit(1);
    }

    int32_t bytes_read = sys_fs_read(source_path, chunk_buffer, (uint32_t)total_size);
    if (bytes_read != total_size) {
        print("setwall: read error\n");
        sys_exit(1);
    }

    print("Applying wallpaper: ");
    print(source_path);
    print(" -> WALLPAPR.BMP\n");

    int32_t written = sys_fs_write("WALLPAPR.BMP", chunk_buffer, (uint32_t)total_size);
    if (written < 0) {
        print("setwall: failed writing WALLPAPR.BMP\n");
        sys_exit(1);
    }

    print("Wallpaper updated successfully.\n");
    sys_exit(0);
}