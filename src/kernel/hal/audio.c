#include "audio.h"
#include "../mutex.h"
#include "../drivers/pc_speaker.h"
#include "../drivers/sb16_driver.h"
#include "../drivers/serial.h"

static const audio_driver_ops_t* active_driver = 0;
static int opl_active, pcm_active;
static const audio_driver_ops_t* devices[4];
static uint32_t device_count, selected, volume = 100;
static uint8_t opl_levels[22];
static uint8_t opl_level(uint8_t value) {
    uint32_t attenuation = (value & 63u) + (100u - volume) * 63u / 100u;
    return (value & 0xC0u) | (attenuation > 63u ? 63u : attenuation);
}

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
    .interrupt = sb16_interrupt,
    .set_volume = sb16_set_volume
};

void audio_hal_register(const audio_driver_ops_t* driver) {
    if (driver && device_count < 4) devices[device_count++] = driver;
}
void audio_hal_init(void) {
    device_count = 0;
    if (sb16_probe() == 0) audio_hal_register(&sb16_ops);
    audio_hal_register(&pc_speaker_ops);
    selected = 0; active_driver = devices[0];
    audio_set_volume(volume);
}
uint32_t audio_device_count(void) { return device_count; }
const char* audio_device_name(uint32_t index) {
    return index < device_count ? devices[index]->name : "";
}
uint32_t audio_device_selected(void) { return selected; }
int32_t audio_device_select(uint32_t index) {
    MUTEX_GUARD(audio_mutex);
    if (index >= device_count) return -1;
    if (index == selected) return 0;
    if (pcm_active || opl_active) return -1;
    selected = index; active_driver = devices[index];
    audio_set_volume(volume);
    return 0;
}
uint32_t audio_get_volume(void) { return volume; }
void audio_set_volume(uint32_t value) {
    MUTEX_GUARD(audio_mutex);
    volume = value > 100 ? 100 : value;
    if (active_driver && active_driver->set_volume) active_driver->set_volume(volume);
    if (opl_active)
        for (uint32_t i = 0; i < 22; i++)
            active_driver->opl_write(0x40 + i, opl_level(opl_levels[i]));
}

int32_t audio_init(uint32_t sample_rate) {
    MUTEX_GUARD(audio_mutex);
    if (opl_active || !active_driver || !active_driver->init) return -1;
    if (pcm_active) return -1;
    int32_t result = active_driver->init(sample_rate);
    if (result == 0) { pcm_active = 1; audio_set_volume(volume); }
    return result;
}

int32_t audio_write(const void* data, uint32_t length) {
    MUTEX_GUARD(audio_mutex);
    if (opl_active || !active_driver || !active_driver->write) return -1;
    return active_driver->write(data, length);
}

int32_t audio_status(void) {
    MUTEX_GUARD(audio_mutex);
    if (!active_driver || !active_driver->status) return -1;
    return active_driver->status();
}

int32_t audio_drain(void) {
    MUTEX_GUARD(audio_mutex);
    if (opl_active || !active_driver || !active_driver->drain) return -1;
    return active_driver->drain();
}

void audio_service(void) {
    MUTEX_GUARD(audio_mutex);
    if (active_driver && active_driver->service) {
        active_driver->service();
    }
}

int32_t audio_opl_init(void) {
    MUTEX_GUARD(audio_mutex);
    if (pcm_active || opl_active) return -1;
    if (!active_driver || !active_driver->opl_init || !active_driver->opl_write) return -1;
    if (active_driver->opl_init() < 0) return -1;
    for (uint32_t i = 0; i < 22; i++) opl_levels[i] = 0;
    opl_active = 1;
    audio_set_volume(volume);
    return 0;
}

int32_t audio_opl_write(uint32_t reg, uint32_t value) {
    MUTEX_GUARD(audio_mutex);
    if (!opl_active || reg > 255 || value > 255) return -1;
    if (reg >= 0x40 && reg <= 0x55) {
        opl_levels[reg - 0x40] = value;
        value = opl_level(value);
    }
    active_driver->opl_write((uint8_t)reg, (uint8_t)value);
    return 0;
}

void audio_shutdown(void) {
    MUTEX_GUARD(audio_mutex);
    pcm_active = 0;
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