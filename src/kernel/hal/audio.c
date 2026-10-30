#include "audio.h"
#include "../mutex.h"
#include "../drivers/pc_speaker.h"
#include "../drivers/sb16_driver.h"
#include "../drivers/hda.h"
#include "../drivers/serial.h"

static const audio_driver_ops_t* active_driver = 0;
static int opl_active, pcm_active;
static const audio_driver_ops_t* devices[4];
static uint32_t device_count, selected, volume = 100;
/* Client calls are logged; periodic service and IRQ polling are excluded.
 * At runtime debug_print only queues UART output, so audio never waits for TX. */
static void audio_trace(const char* call, uint32_t a, uint32_t b, const char* phase, int32_t result) {
    uint32_t flags = task_irq_save();
    debug_audio_print("[AUDIO] "); debug_audio_print(call); debug_audio_print(" "); debug_audio_print(phase);
    debug_audio_print(" device="); debug_audio_print(active_driver ? active_driver->name : "none");
    debug_audio_print(" a="); debug_audio_hex(a); debug_audio_print(" b="); debug_audio_hex(b);
    debug_audio_print(" result="); debug_audio_hex((uint32_t)result); debug_audio_print("\n");
    task_irq_restore(flags);
}
static int32_t audio_result(const char* call, uint32_t a, uint32_t b, int32_t result) {
    audio_trace(call, a, b, "return", result);
    return result;
}
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
    .pending_frames = sb16_pending_frames,
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
    if (hda_probe() == 0) audio_hal_register(&hda_ops);
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
    audio_trace("audio_device_select", index, 0, "call", 0);
    if (index >= device_count) return audio_result("audio_device_select", index, 0, -1);
    if (index == selected) return audio_result("audio_device_select", index, 0, 0);
    if (pcm_active || opl_active) return audio_result("audio_device_select", index, 0, -1);
    selected = index; active_driver = devices[index];
    audio_set_volume(volume);
    return audio_result("audio_device_select", index, 0, 0);
}
uint32_t audio_get_volume(void) { return volume; }
void audio_set_volume(uint32_t value) {
    MUTEX_GUARD(audio_mutex);
    audio_trace("audio_set_volume", value, 0, "call", 0);
    volume = value > 100 ? 100 : value;
    if (active_driver && active_driver->set_volume) active_driver->set_volume(volume);
    if (opl_active)
        for (uint32_t i = 0; i < 22; i++)
            active_driver->opl_write(0x40 + i, opl_level(opl_levels[i]));
}

int32_t audio_init(uint32_t sample_rate) {
    MUTEX_GUARD(audio_mutex);
    audio_trace("audio_init", sample_rate, 0, "call", 0);
    if (opl_active || !active_driver || !active_driver->init) return audio_result("audio_init", sample_rate, 0, -1);
    if (pcm_active) return audio_result("audio_init", sample_rate, 0, -1);
    int32_t result = active_driver->init(sample_rate);
    if (result == 0) { pcm_active = 1; audio_set_volume(volume); }
    return audio_result("audio_init", sample_rate, 0, result);
}

int32_t audio_write(const void* data, uint32_t length) {
    MUTEX_GUARD(audio_mutex);
    audio_trace("audio_write", length, 0, "call", 0);
    if (opl_active || !active_driver || !active_driver->write) return audio_result("audio_write", length, 0, -1);
    return audio_result("audio_write", length, 0, active_driver->write(data, length));
}

int32_t audio_status(void) {
    MUTEX_GUARD(audio_mutex);
    audio_trace("audio_status", 0, 0, "call", 0);
    if (!active_driver || !active_driver->status) return audio_result("audio_status", 0, 0, -1);
    return audio_result("audio_status", 0, 0, active_driver->status());
}

int32_t audio_pending_frames(void) {
    MUTEX_GUARD(audio_mutex);
    if (!pcm_active || !active_driver || !active_driver->pending_frames) return -1;
    return active_driver->pending_frames();
}

int32_t audio_drain(void) {
    MUTEX_GUARD(audio_mutex);
    audio_trace("audio_drain", 0, 0, "call", 0);
    if (opl_active || !active_driver || !active_driver->drain) return audio_result("audio_drain", 0, 0, -1);
    return audio_result("audio_drain", 0, 0, active_driver->drain());
}

void audio_service(void) {
    MUTEX_GUARD(audio_mutex);
    if (active_driver && active_driver->service) {
        active_driver->service();
    }
}

int32_t audio_opl_init(void) {
    MUTEX_GUARD(audio_mutex);
    audio_trace("audio_opl_init", 0, 0, "call", 0);
    if (pcm_active || opl_active) return audio_result("audio_opl_init", 0, 0, -1);
    if (!active_driver || !active_driver->opl_init || !active_driver->opl_write) return audio_result("audio_opl_init", 0, 0, -1);
    if (active_driver->opl_init() < 0) return audio_result("audio_opl_init", 0, 0, -1);
    for (uint32_t i = 0; i < 22; i++) opl_levels[i] = 0;
    opl_active = 1;
    audio_set_volume(volume);
    return audio_result("audio_opl_init", 0, 0, 0);
}

int32_t audio_opl_write(uint32_t reg, uint32_t value) {
    MUTEX_GUARD(audio_mutex);
    audio_trace("audio_opl_write", reg, value, "call", 0);
    if (!opl_active || reg > 255 || value > 255) return audio_result("audio_opl_write", reg, value, -1);
    if (reg >= 0x40 && reg <= 0x55) {
        opl_levels[reg - 0x40] = value;
        value = opl_level(value);
    }
    active_driver->opl_write((uint8_t)reg, (uint8_t)value);
    return audio_result("audio_opl_write", reg, value, 0);
}

void audio_shutdown(void) {
    MUTEX_GUARD(audio_mutex);
    audio_trace("audio_shutdown", 0, 0, "call", 0);
    pcm_active = 0;
    if (opl_active) {
        for (uint8_t i = 0; i < 9; i++) {
            active_driver->opl_write(0xB0 + i, 0);
        }
        for (uint8_t i = 0; i < 22; i++) active_driver->opl_write(0x40 + i, 63);
        opl_active = 0;
        audio_trace("audio_shutdown", 0, 0, "return", 0);
        return;
    }
    if (active_driver && active_driver->shutdown) {
        active_driver->shutdown();
    }
    audio_trace("audio_shutdown", 0, 0, "return", 0);
}

void audio_interrupt(uint8_t int_no) {
    if (int_no == 37 && active_driver && active_driver->interrupt) {
        active_driver->interrupt();
    }
}