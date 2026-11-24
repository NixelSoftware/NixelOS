#include "../../system/user/sys_api.h"

void user_main(const char* arguments) {
    (void)arguments;
    int32_t result = sys_shutdown();
    const char* message = result == SYS_ERR_PERMISSION ?
        "shutdown: permission denied; run sudo shutdown.\n" :
        "shutdown: power operation failed.\n";
    uint32_t length = 0;
    while (message[length]) length++;
    (void)sys_write(message, length);
}
