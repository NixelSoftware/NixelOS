#include "../../kernel/mutex.h"
#include "app_loader.h"
#include "../../kernel/task.h"
#include "../../kernel/paging.h"
#include "../../kernel/net/ipv4.h"
#include "../../kernel/drivers/serial.h"
#include "../../kernel/arch_i386.h"
#include "../../kernel/syscall.h"
#include "../graphics/wm/wm.h"

#define APP_LOAD_LIMIT   1048576u
#define APP_BSS_LIMIT    0x00DF0000u
#define APP_ARGUMENT_MAX 256u
#define APP_INSTANCE_MAX 8
#define APP_MAPPED_PAGES ((APP_BSS_LIMIT - APP_VIRTUAL_BASE) / PAGE_SIZE)

typedef int (*app_entry_fn)(os_api_t* api);

typedef struct {
    address_space_t address_space;
    uint8_t active;
    uint8_t pending_release;
    uint8_t running;
    uint8_t type;
    uint32_t entry, user_stack;
    os_api_t* api;
    uint16_t window_references;
    uint32_t context_references;
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

uint32_t app_loader_context_enter(uint32_t directory) {
    MUTEX_GUARD(kernel_mutex);
    uint32_t previous = paging_current_directory();
    int index = app_instance_find(previous);
    /* The previous mapping lives on the caller's stack until leave, including
     * across nested callbacks and sleeps in a different address space. */
    if (index >= 0) app_instances[index].context_references++;
    paging_activate_directory(directory ? directory : paging_kernel_directory());
    return previous;
}

void app_loader_context_leave(uint32_t previous) {
    MUTEX_GUARD(kernel_mutex);
    paging_activate_directory(previous);
    int index = app_instance_find(previous);
    if (index >= 0 && app_instances[index].context_references)
        app_instances[index].context_references--;
}

void app_loader_window_attached(uint32_t directory_address) {
    MUTEX_GUARD(kernel_mutex);
    int index = app_instance_find(directory_address);
    if (index >= 0) app_instances[index].window_references++;
}

void app_loader_window_detached(uint32_t directory_address) {
    MUTEX_GUARD(kernel_mutex);
    int index = app_instance_find(directory_address);
    if (index < 0) return;
    if (app_instances[index].window_references) app_instances[index].window_references--;
    if (!app_instances[index].window_references)
        app_instances[index].pending_release = 1;
}

void app_loader_collect(void) {
    MUTEX_GUARD(kernel_mutex);
    for (int i = 0; i < APP_INSTANCE_MAX; i++) {
        app_instance_t* instance = &app_instances[i];
        if (!instance->active || instance->running || !instance->pending_release ||
            instance->window_references || instance->context_references)
            continue;
        /* kernel_mutex serializes app lifecycle/callback dispatch. Also retain
         * mappings referenced by a suspended task, not just the active CR3. */
        if (task_address_space_in_use((uint32_t)instance->address_space.directory))
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
            app_instances[i].running = 0;
            app_instances[i].window_references = 0;
            app_instances[i].context_references = 0;
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

/* Runs on a private kernel stack, independently of the input callback that
 * launched it. A running task pins its mappings even if its last window closes. */
static void app_run(void* argument) {
    app_instance_t* instance = argument;
    paging_activate(&instance->address_space);
    if (instance->type == APP_TYPE_USER) {
        arch_i386_enter_user(instance->entry, instance->user_stack);
        wm_user_close_address_space((uint32_t)instance->address_space.directory);
    } else {
        ((app_entry_fn)instance->entry)(instance->api);
    }
    debug_print("[APP] executable returned\n");
    paging_activate_kernel();
    mutex_lock(&kernel_mutex);
    instance->running = 0;
    if (!instance->window_references) instance->pending_release = 1;
    app_loader_collect();
    mutex_unlock(&kernel_mutex);
    task_exit();
}

static int app_loader_prepare(os_api_t* api, const char* name, const char* arguments) {
    MUTEX_GUARD(kernel_mutex);
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
    debug_print("[APP] activating address space\n");
    uint32_t previous_directory = app_loader_context_enter((uint32_t)instance->address_space.directory);
    debug_print("[APP] reading executable\n");
    /* Hold no core/WM lock across disk access. This private mapping belongs
     * to the loader task; reaping is excluded until preparation completes. */
    mutex_unlock(&kernel_mutex);
    int bytes = read_app(api, name, (uint8_t*)filename);
    mutex_lock(&kernel_mutex);
    debug_print("[APP] executable read returned\n");
    if (bytes <= (int)sizeof(app_header_t)) {
        app_loader_context_leave(previous_directory);
        instance->pending_release = 1;
        app_loader_collect();
        return -1;
    }

    app_header_t* header = (app_header_t*)APP_VIRTUAL_BASE;
    if (header->magic != APP_HEADER_MAGIC ||
        (header->type != APP_TYPE_GUI && header->type != APP_TYPE_CONSOLE &&
         header->type != APP_TYPE_USER)) {
        app_loader_context_leave(previous_directory);
        instance->pending_release = 1;
        app_loader_collect();
        return -2;
    }

    app_entry_fn entry = (app_entry_fn)(APP_VIRTUAL_BASE + sizeof(app_header_t));
    instance->entry = (uint32_t)entry;
    instance->type = header->type;
    instance->api = api;
    if (header->type == APP_TYPE_USER) {
        uint32_t argument_length = 0;
        const char* source = arguments ? arguments : "";
        while (source[argument_length] && argument_length < APP_ARGUMENT_MAX - 1u)
            argument_length++;
        if (source[argument_length]) {
            app_loader_context_leave(previous_directory);
            instance->pending_release = 1;
            app_loader_collect();
            return -1;
        }

        uint32_t user_arguments = APP_BSS_LIMIT - 512u;
        for (uint32_t i = 0; i < argument_length; i++)
            ((char*)user_arguments)[i] = source[i];
        ((char*)user_arguments)[argument_length] = '\0';

        uint32_t user_stack = APP_BSS_LIMIT - 32u;
        /* Match a cdecl call frame: a return address, then the first argument. */
        *(uint32_t*)user_stack = 0;
        *(uint32_t*)(user_stack + sizeof(uint32_t)) = user_arguments;
        instance->user_stack = user_stack;
    }
    app_loader_context_leave(previous_directory);
    instance->running = 1;
    if (task_create(app_run, instance, name) < 0) {
        instance->running = 0;
        instance->pending_release = 1;
        app_loader_collect();
        return -3;
    }
    debug_print("[APP] queued executable task\n");
    return 0;
}

#define APP_LAUNCH_QUEUE 8u
typedef struct {
    os_api_t* api;
    char name[FS_MAX_FILENAME];
    char arguments[APP_ARGUMENT_MAX];
    uint8_t active;
} launch_request_t;
static launch_request_t launch_requests[APP_LAUNCH_QUEUE];
extern void kernel_user_write(const char*, uint32_t);

static void app_load_worker(void* argument) {
    launch_request_t* request = argument;
    int result = app_loader_prepare(request->api, request->name, request->arguments);
    if (result < 0) {
        static const char message[] = "Cannot launch application (missing, invalid, or resources exhausted).\n";
        kernel_user_write(message, sizeof(message)-1u);
    }
    mutex_lock(&kernel_mutex);
    request->active = 0;
    mutex_unlock(&kernel_mutex);
}
int app_loader_launch(os_api_t* api, const char* name, const char* arguments) {
    if (!api || !api->fs.read || !name || !*name) return -1;
    MUTEX_GUARD(kernel_mutex);
    launch_request_t* request = 0;
    for (uint32_t i = 0; i < APP_LAUNCH_QUEUE; i++)
        if (!launch_requests[i].active) { request = &launch_requests[i]; break; }
    if (!request) return -3;
    uint32_t n = 0;
    while (name[n] && n < sizeof(request->name)-1u) { request->name[n] = name[n]; n++; }
    if (name[n]) return -1;
    request->name[n] = 0;
    n = 0;
    const char* source = arguments ? arguments : "";
    while (source[n] && n < sizeof(request->arguments)-1u) { request->arguments[n] = source[n]; n++; }
    if (source[n]) return -1;
    request->arguments[n] = 0;
    request->api = api; request->active = 1;
    if (task_create(app_load_worker, request, "app-loader") < 0) { request->active = 0; return -3; }
    return 0;
}
