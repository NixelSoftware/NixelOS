#ifndef SYSTEM_USER_MEMORY_API_H
#define SYSTEM_USER_MEMORY_API_H

#include "../../kernel/types.h"

/* Byte counts for the configured physical memory layout, not BIOS detection.
 * used_bytes includes reserved_bytes; available_bytes is allocatable now.
 * total_bytes == used_bytes + available_bytes.
 */
typedef struct {
    uint32_t total_bytes;
    uint32_t used_bytes;
    uint32_t available_bytes;
    uint32_t reserved_bytes;
} user_memory_info_t;

#endif
