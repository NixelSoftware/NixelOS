#include "audio.h"
#include "../drivers/sb16_driver.h"
#include "../drivers/serial.h"

static const audio_driver_ops_t* active_driver = 0;
static int opl_active;

static const audio_driver_ops_t sb16_ops = {
    .name      = "Sound Blaster 16",
    .opl_init = sb16_opl_init,
    .opl_write = sb16_opl_write,
    .init      = sb16_init,
    .write     = sb16_write,
    .status    = sb16_status,
    .drain     = sb16_drain,
    .service   = sb16_service,
    .shutdown  = sb16_shutdown,
    .interrupt = sb16_interrupt
};

void audio_hal_register(const audio_driver_ops_t* driver) {
    active_driver = driver;
}

void audio_hal_init(void) {
    active_driver = &sb16_ops;
    debug_print("[AUDIO HAL] Initialized with driver: ");
    debug_print(active_driver->name);
    debug_print("\n");
}

int32_t audio_init(uint32_t sample_rate) {
    if (opl_active || !active_driver || !active_driver->init) return -1;
    return active_driver->init(sample_rate);
}

int32_t audio_write(const void* data, uint32_t length) {
    if (opl_active || !active_driver || !active_driver->write) return -1;
    return active_driver->write(data, length);
}

int32_t audio_status(void) {
    if (!active_driver || !active_driver->status) return -1;
    return active_driver->status();
}

int32_t audio_drain(void) {
    if (opl_active || !active_driver || !active_driver->drain) return -1;
    return active_driver->drain();
}

void audio_service(void) {
    if (active_driver && active_driver->service) {
        active_driver->service();
    }
}

int32_t audio_opl_init(void) {
    if (!active_driver || !active_driver->opl_init || !active_driver->opl_write) return -1;
    if (active_driver->opl_init() < 0) return -1;
    opl_active = 1;
    return 0;
}

int32_t audio_opl_write(uint32_t reg, uint32_t value) {
    if (!opl_active || reg > 255 || value > 255) return -1;
    active_driver->opl_write((uint8_t)reg, (uint8_t)value);
    return 0;
}

void audio_shutdown(void) {
    if (opl_active) {
        for (uint8_t i = 0; i < 9; i++) {
            active_driver->opl_write(0xB0 + i, 0);
        }
        for (uint8_t i = 0; i < 22; i++) active_driver->opl_write(0x40 + i, 63);
        opl_active = 0;
        return;
    }
    if (active_driver && active_driver->shutdown) {
        active_driver->shutdown();
    }
}

void audio_interrupt(uint8_t int_no) {
    if (int_no == 37 && active_driver && active_driver->interrupt) {
        active_driver->interrupt();
    }
}