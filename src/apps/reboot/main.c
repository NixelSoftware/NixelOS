#include "../../system/user/sys_api.h"
#include "../../kernel/os_api.h"

void user_main(os_api_t* api) {
    static const char message[] = "Rebooting the system...\n";
    (void)sys_write(message, sizeof(message) - 1u);
    sys_reboot();
}
