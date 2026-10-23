#include "audio.h"
#include "../drivers/sb16_driver.h"
#include "../drivers/serial.h"

static const audio_driver_ops_t* active_driver = 0;

static const audio_driver_ops_t sb16_ops = {
    .name      = "Sound Blaster 16",
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
    if (!active_driver || !active_driver->init) return -1;
    return active_driver->init(sample_rate);
}

int32_t audio_write(const void* data, uint32_t length) {
    if (!active_driver || !active_driver->write) return -1;
    return active_driver->write(data, length);
}

int32_t audio_status(void) {
    if (!active_driver || !active_driver->status) return -1;
    return active_driver->status();
}

int32_t audio_drain(void) {
    if (!active_driver || !active_driver->drain) return -1;
    return active_driver->drain();
}

void audio_service(void) {
    if (active_driver && active_driver->service) {
        active_driver->service();
    }
}

void audio_shutdown(void) {
    if (active_driver && active_driver->shutdown) {
        active_driver->shutdown();
    }
}

void audio_interrupt(uint8_t int_no) {
    if (int_no == 37 && active_driver && active_driver->interrupt) {
        active_driver->interrupt();
    }
}