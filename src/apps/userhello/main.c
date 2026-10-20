#include "../../system/user/sys_api.h"

void user_main(void) {
    static const char message[] = "[USER] ring 3 syscall path reached\n";
    (void)sys_get_ticks();
    sys_write(message, sizeof(message) - 1u);
}
