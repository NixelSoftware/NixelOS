#ifndef NH_CORE_UTIL_DEBUG_H
#define NH_CORE_UTIL_DEBUG_H

// LICENSE =========================================================================================

/**
 * Netzhaut - Web Browser Engine
 * Copyright (C) 2022  Dajo Frey
 * Published under GNU LGPL. See Netzhaut/LICENSE.LGPL file.
 */

// INCLUDES ========================================================================================

#include <stdio.h>

#ifdef NH_NIXEL
#define NH_CORE_STRINGIFY_(x) #x
#define NH_CORE_STRINGIFY(x) NH_CORE_STRINGIFY_(x)
#define NH_CORE_REPORT(message) nh_core_debug(message " at " __FILE__ ":" NH_CORE_STRINGIFY(__LINE__))
#else
#define NH_CORE_REPORT(message) do { \
    char message_p[255]; \
    sprintf(message_p, message " at line %d (%s) in %s failed.", __LINE__, __func__, __FILE__); \
    nh_core_debug(message_p); \
} while (0)
#endif

// DEFINES =========================================================================================

#define NH_CORE_CHECK(checkable) \
{ \
    NH_API_RESULT checkResult = checkable; \
    if (checkResult != NH_API_SUCCESS) { \
        NH_CORE_REPORT("Check"); \
        return checkResult; \
    } \
} 
 
#define NH_CORE_CHECK_2(checkReturn, checkable) \
{ \
    NH_API_RESULT checkResult = checkable; \
    if (checkResult != NH_API_SUCCESS) { \
        NH_CORE_REPORT("Check"); \
        return checkReturn; \
    } \
} 
 
#define NH_CORE_CHECK_NULL(checkable) \
{ \
    void *checkResult_p = checkable; \
    if (checkResult_p == NULL) { \
        NH_CORE_REPORT("Check (NULL)"); \
        return NH_API_ERROR_NULL_POINTER; \
    } \
} 

#define NH_CORE_CHECK_NULL_2(checkReturn, checkable) \
{ \
    void *checkResult_p = checkable; \
    if (checkResult_p == NULL) { \
        NH_CORE_REPORT("Check (NULL)"); \
        return checkReturn; \
    } \
} 
 
#define NH_CORE_CHECK_MEM(checkable) \
{ \
    void *checkResult_p = checkable; \
    if (checkResult_p == NULL) { \
        NH_CORE_REPORT("Check (MEM)"); \
        return NH_API_ERROR_MEMORY_ALLOCATION; \
    } \
} 
 
#define NH_CORE_CHECK_MEM_2(checkReturn, checkable) \
{ \
    void *checkResult_p = checkable; \
    if (checkResult_p == NULL) { \
        NH_CORE_REPORT("Check (MEM)"); \
        return checkReturn; \
    } \
} 

// FUNCTIONS =======================================================================================

void nh_core_log(
    char *node_p, char *option_p, char *message_p
);

void nh_core_debug(
    char *message_p
);

#endif // NH_CORE_UTIL_DEBUG_H
