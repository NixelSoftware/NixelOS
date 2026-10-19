#include "../../kernel/types.h"
#include "../../kernel/os_api.h"

int main_app(os_api_t* os) {
    if (os) {
        os->term_print("Triggering kernel panic...\n");
    }

    __asm__ volatile ("ud2");

    return 0;
}