#include "../../system/user/sys_api.h"

void user_main(void) {
    static const char message[] = "Triggering user-mode page fault; the app should terminate safely.\n";
    (void)sys_write(message, sizeof(message) - 1u);

    volatile uint32_t value = *(volatile uint32_t*)0x01000000u;
    (void)value;
    sys_exit(0);
}
