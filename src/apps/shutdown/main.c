#include "../../kernel/types.h"
#include "../../kernel/os_api.h"

int main_app(os_api_t* os) {
    if (!os) return -1;

    os->term_print("Shutting down the system...\n");

    if (os->shutdown) {
        os->shutdown();
    }

    return 0;
}