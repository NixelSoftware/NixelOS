#include "app_loader.h"
#include "../../kernel/paging.h"
#include "../../kernel/net/ipv4.h"
#include "../../kernel/drivers/serial.h"
#include "../../kernel/arch_i386.h"
#include "../../kernel/syscall.h"
#include "../graphics/wm/wm.h"

#define APP_LOAD_LIMIT   1048576u
#define APP_BSS_LIMIT    0x005F0000u
#define APP_ARGUMENT_MAX 256u
#define APP_INSTANCE_MAX 8
#define APP_MAPPED_PAGES ((APP_BSS_LIMIT - APP_VIRTUAL_BASE) / PAGE_SIZE)

typedef int (*app_entry_fn)(os_api_t* api);

typedef struct {
    address_space_t address_space;
    uint8_t active;
    uint8_t pending_release;
    uint16_t window_references;
} app_instance_t;

static app_instance_t app_instances[APP_INSTANCE_MAX];

static int app_instance_find(uint32_t directory_address) {
    for (int i = 0; i < APP_INSTANCE_MAX; i++) {
        if (app_instances[i].active &&
            (uint32_t)app_instances[i].address_space.directory == directory_address)
            return i;
    }
    return -1;
}

void app_loader_window_attached(uint32_t directory_address) {
    int index = app_instance_find(directory_address);
    if (index >= 0) app_instances[index].window_references++;
}

void app_loader_window_detached(uint32_t directory_address) {
    int index = app_instance_find(directory_address);
    if (index < 0) return;
    if (app_instances[index].window_references) app_instances[index].window_references--;
    if (!app_instances[index].window_references)
        app_instances[index].pending_release = 1;
}

void app_loader_collect(void) {
    for (int i = 0; i < APP_INSTANCE_MAX; i++) {
        app_instance_t* instance = &app_instances[i];
        if (!instance->active || !instance->pending_release || instance->window_references)
            continue;
        if (paging_current_directory() == (uint32_t)instance->address_space.directory)
            continue;
        syscall_forget_address_space((uint32_t)instance->address_space.directory);
        ipv4_clear_handlers_for_directory((uint32_t)instance->address_space.directory);
        paging_address_space_destroy(&instance->address_space);
        instance->active = 0;
        instance->pending_release = 0;
    }
}

static int app_instance_reserve(void) {
    for (int i = 0; i < APP_INSTANCE_MAX; i++) {
        if (!app_instances[i].active) {
            app_instances[i].active = 1;
            app_instances[i].pending_release = 0;
            app_instances[i].window_references = 0;
            return i;
        }
    }
    return -1;
}

static int read_app(os_api_t* api, const char* name, uint8_t* destination) {
    int length = 0;
    while (name[length] && length < FS_MAX_FILENAME - 1) {
        destination[length] = (uint8_t)name[length];
        length++;
    }
    if (name[length]) return -1;

    int bytes = api->fs.read(name, (uint8_t*)APP_VIRTUAL_BASE, APP_LOAD_LIMIT);
    if (bytes > 0) return bytes;

    if (length >= FS_MAX_FILENAME - 5) return bytes;
    destination[length++] = '.';
    destination[length++] = 'b';
    destination[length++] = 'i';
    destination[length++] = 'n';
    destination[length] = '\0';
    return api->fs.read((const char*)destination, (uint8_t*)APP_VIRTUAL_BASE,
                        APP_LOAD_LIMIT);
}

int app_loader_launch(os_api_t* api, const char* name, const char* arguments) {
    if (!api || !api->fs.read || !name || !*name) return -1;

    app_loader_collect();
    int slot = app_instance_reserve();
    if (slot < 0) return -3;

    app_instance_t* instance = &app_instances[slot];
    if (paging_address_space_create(&instance->address_space) != 0) {
        instance->active = 0;
        return -4;
    }

    for (uint32_t page = 0; page < APP_MAPPED_PAGES; page++) {
        uint32_t virtual_address = APP_VIRTUAL_BASE + page * PAGE_SIZE;
        if (paging_address_space_alloc_page(&instance->address_space, virtual_address,
                                            1, 0) != 0) {
            paging_address_space_destroy(&instance->address_space);
            instance->active = 0;
            return -4;
        }
    }

    char filename[FS_MAX_FILENAME];
    uint32_t kernel_directory = paging_kernel_directory();
    debug_print("[APP] activating address space\n");
    paging_activate(&instance->address_space);
    debug_print("[APP] reading executable\n");
    int bytes = read_app(api, name, (uint8_t*)filename);
    debug_print("[APP] executable read returned\n");
    if (bytes <= (int)sizeof(app_header_t)) {
        paging_activate_directory(kernel_directory);
        instance->pending_release = 1;
        app_loader_collect();
        return -1;
    }

    app_header_t* header = (app_header_t*)APP_VIRTUAL_BASE;
    if (header->magic != APP_HEADER_MAGIC ||
        (header->type != APP_TYPE_GUI && header->type != APP_TYPE_CONSOLE &&
         header->type != APP_TYPE_USER)) {
        paging_activate_directory(kernel_directory);
        instance->pending_release = 1;
        app_loader_collect();
        return -2;
    }

    app_entry_fn entry = (app_entry_fn)(APP_VIRTUAL_BASE + sizeof(app_header_t));
    debug_print("[APP] entering executable\n");
    if (header->type == APP_TYPE_USER) {
        uint32_t argument_length = 0;
        const char* source = arguments ? arguments : "";
        while (source[argument_length] && argument_length < APP_ARGUMENT_MAX - 1u)
            argument_length++;
        if (source[argument_length]) {
            paging_activate_directory(kernel_directory);
            instance->pending_release = 1;
            app_loader_collect();
            return -1;
        }

        uint32_t user_arguments = APP_BSS_LIMIT - 512u;
        for (uint32_t i = 0; i < argument_length; i++)
            ((char*)user_arguments)[i] = source[i];
        ((char*)user_arguments)[argument_length] = '\0';

        uint32_t user_stack = APP_BSS_LIMIT - 32u;
        *(uint32_t*)user_stack = user_arguments;
        arch_i386_enter_user((uint32_t)entry, user_stack);
        wm_user_close_address_space((uint32_t)instance->address_space.directory);
    } else {
        entry(api);
    }
    debug_print("[APP] executable returned\n");
    paging_activate_directory(kernel_directory);

    if (!instance->window_references) instance->pending_release = 1;
    app_loader_collect();
    return 0;
}
