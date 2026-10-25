/**
 * Netzhaut - Web Browser 
 * Copyright (C) 2022  Dajo Frey
 * This file is published under MIT.
 */

#include "nh-api/nh-api.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

int main(
    int argc, char **argv_pp) 
{
    (void)argc;
    (void)argv_pp;

    const char *corePath_p = getenv("NETZHAUT_CORE");
    NH_API_RESULT result = nh_api_initialize(
        (char *)corePath_p, NULL, 0
    );
    if (result != NH_API_SUCCESS) {
        fputs("Failed to initialize Netzhaut core.\n", stderr);
        return 1;
    }

    int runResult = nh_api_run();
    NH_API_RESULT terminateResult = nh_api_terminate();
    if (runResult < 0 || terminateResult != NH_API_SUCCESS) {
        fputs("Netzhaut terminated with an error.\n", stderr);
        return 1;
    }

    return 0;
}
