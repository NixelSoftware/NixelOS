#include "../../system/user/sys_api.h"

void user_main(void) {
    static const char message[] = "Shutting down the system...\n";
    (void)sys_write(message, sizeof(message) - 1u);
    sys_shutdown();
}
