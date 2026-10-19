#include "../../kernel/types.h"
#include "../../kernel/os_api.h"

int main_app(os_api_t* os) {
    if (!os) return -1;

    os->shell.term_print("Shutting down the system...\n");

    if (os->system.shutdown) {
        os->system.shutdown();
    }

    return 0;
}