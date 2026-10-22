#include "../../system/user/sys_api.h"
#include "../../kernel/types.h"

#define MAX_IMAGE_SIZE 1572864u

static uint8_t image_buf[MAX_IMAGE_SIZE];

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

    if ((uint32_t)total_size > sizeof(image_buf)) {
        print("setwall: file exceeds maximum supported wallpaper size (1.5 MB)\n");
        sys_exit(1);
    }

    int32_t bytes_read = sys_fs_read(source_path, image_buf, (uint32_t)total_size);
    if (bytes_read != total_size) {
        print("setwall: failed to read complete source image\n");
        sys_exit(1);
    }

    if (image_buf[0] != 'B' || image_buf[1] != 'M') {
        print("setwall: file is not a valid BMP image (missing BM header)\n");
        sys_exit(1);
    }

    print("Applying wallpaper: ");
    print(source_path);
    print(" -> WALLPAPR.BMP\n");

    int32_t written = sys_fs_write("WALLPAPR.BMP", image_buf, (uint32_t)total_size);
    if (written < 0) {
        print("setwall: failed writing to WALLPAPR.BMP on disk\n");
        sys_exit(1);
    }

    print("Success: Wallpaper saved as WALLPAPR.BMP. Reboot to apply.\n");
    sys_exit(0);
}