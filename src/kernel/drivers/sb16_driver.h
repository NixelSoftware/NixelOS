#ifndef SB16_DRIVER_H
#define SB16_DRIVER_H
#include "../types.h"
#define SB16_BUFFER_SIZE 16384u
int32_t sb16_init(uint32_t sample_rate);
/* Returns bytes accepted, 0 when both slots are full, -1 on error. */
int32_t sb16_write(const void* data, uint32_t length);
int32_t sb16_status(void); /* 1 playing, 0 idle, -1 closed */
void sb16_shutdown(void);
void sb16_interrupt(void);
#endif
