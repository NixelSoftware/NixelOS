#include "app_loader.h"

#define APP_LOAD_ADDRESS 0x00200000u
#define APP_LOAD_LIMIT   1048576u
#define APP_BSS_LIMIT    0x003F0000u

typedef int (*app_entry_fn)(os_api_t* api);

int app_loader_launch(os_api_t* api, const char* name, const char* arguments) {
    if (!api || !api->fs.read || !name || !*name) return -1;

    char filename[FS_MAX_FILENAME];
    int name_length = 0;
    while (name[name_length] && name_length < FS_MAX_FILENAME - 5) {
        filename[name_length] = name[name_length];
        name_length++;
    }
    filename[name_length] = '\0';

    uint8_t* app_memory = (uint8_t*)APP_LOAD_ADDRESS;
    int bytes = api->fs.read(filename, app_memory, APP_LOAD_LIMIT);
    if (bytes <= 0) {
        filename[name_length++] = '.';
        filename[name_length++] = 'b';
        filename[name_length++] = 'i';
        filename[name_length++] = 'n';
        filename[name_length] = '\0';
        bytes = api->fs.read(filename, app_memory, APP_LOAD_LIMIT);
    }
    if (bytes <= (int)sizeof(app_header_t)) return -1;

    app_header_t* header = (app_header_t*)app_memory;
    if (header->magic != APP_HEADER_MAGIC ||
        (header->type != APP_TYPE_GUI && header->type != APP_TYPE_CONSOLE)) return -2;

    uint32_t bss_start = APP_LOAD_ADDRESS + (uint32_t)bytes;
    if (bss_start < APP_BSS_LIMIT) {
        uint8_t* bss = (uint8_t*)bss_start;
        for (uint32_t address = bss_start; address < APP_BSS_LIMIT; address++)
            *bss++ = 0;
    }

    (void)arguments;
    app_entry_fn entry = (app_entry_fn)(app_memory + sizeof(app_header_t));
    entry(api);
    return 0;
}
