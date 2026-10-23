#ifndef KERNEL_HAL_AUDIO_H
#define KERNEL_HAL_AUDIO_H

#include "../types.h"

typedef struct {
    const char* name;
    int32_t (*init)(uint32_t sample_rate);
    int32_t (*write)(const void* data, uint32_t length);
    int32_t (*status)(void);
    int32_t (*drain)(void);
    void    (*service)(void);
    void    (*shutdown)(void);
    void    (*interrupt)(void);
} audio_driver_ops_t;

void     audio_hal_init(void);
void     audio_hal_register(const audio_driver_ops_t* driver);

int32_t  audio_init(uint32_t sample_rate);
int32_t  audio_write(const void* data, uint32_t length);
int32_t  audio_status(void);
int32_t  audio_drain(void);
void     audio_service(void);
void     audio_shutdown(void);
void     audio_interrupt(uint8_t int_no);

#endif