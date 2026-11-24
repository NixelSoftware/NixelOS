#ifndef SYSTEM_INIT_NIXINIT_H
#define SYSTEM_INIT_NIXINIT_H

typedef struct {
    void (*show_status)(const char* text, int percent);
    void (*initialize_interrupts)(void);
    void (*initialize_timer)(void);
    void (*initialize_acpi)(void);
    void (*initialize_network_device)(void);
    void (*initialize_network_stack)(void);
    void (*mount_filesystem)(void);
    void (*initialize_tasking)(void);
    void (*initialize_system_apps)(void);
    void (*initialize_input)(void);
    void (*finish_boot)(void);
} nixinit_services_t;

int nixinit_has_wm(void);
void nixinit_session_task(void* arg);

void nixinit_start(const nixinit_services_t* services);

#endif
