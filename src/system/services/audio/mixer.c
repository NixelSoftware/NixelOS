#include "mixer.h"
#include "../../../kernel/hal/audio.h"
#include "../../../kernel/mutex.h"

#define STREAMS 16u
#define QUEUE_FRAMES 4096u
#define BLOCK_FRAMES 1024u
#define AHEAD_FRAMES (3u * BLOCK_FRAMES)

typedef struct {
    uint32_t owner, rate, channels, format, bytes_per_frame;
    uint32_t head, count, phase;
    uint64_t last_frame;
    uint8_t active, draining;
    int16_t samples[QUEUE_FRAMES][2];
} mixer_stream_t;
static mixer_stream_t streams[STREAMS];
static mutex_t mixer_mutex;
static int16_t output[BLOCK_FRAMES * 2];
static uint32_t output_rate, pending_bytes, pending_offset;
static uint64_t submitted_frames;
static uint8_t device_open, failed;

static mixer_stream_t* find(uint32_t owner) {
    for (uint32_t i = 0; i < STREAMS; i++)
        if (streams[i].active && streams[i].owner == owner)
            return &streams[i];
    return 0;
}
static int any_streams(void) {
    for (uint32_t i = 0; i < STREAMS; i++)
        if (streams[i].active)
            return 1;
    return 0;
}
static int open_output(void) {
    static const uint32_t rates[] = {48000, 44100, 32000, 22050, 16000, 11025, 8000};
    for (uint32_t i = 0; i < sizeof(rates) / sizeof(rates[0]); i++)
        if (audio_init(rates[i]) == 0) {
            output_rate = rates[i];
            device_open = 1;
            failed = 0;
            return 0;
        }
    failed = 1;
    return -1;
}
static void reset_output_timeline(uint32_t previous_rate) {
    submitted_frames = 0;
    pending_bytes = pending_offset = 0;
    for (uint32_t i = 0; i < STREAMS; i++)
        if (streams[i].active) {
            streams[i].last_frame = 0;
            streams[i].phase = previous_rate ? streams[i].phase * output_rate / previous_rate : 0;
        }
}
int32_t audio_mixer_open(uint32_t owner, const user_audio_format_t* format) {
    /* HDA may allocate DMA frames while opening; keep allocation lock order. */
    MUTEX_GUARD(kernel_mutex);
    mutex_lock(&mixer_mutex);
    int32_t result = -1;
    if (!format || format->sample_rate < 4000 || format->sample_rate > 192000 ||
        format->channels < 1 || format->channels > 2 || format->sample_format < USER_AUDIO_U8 ||
        format->sample_format > USER_AUDIO_S32_LE || find(owner))
        goto done;
    mixer_stream_t* stream = 0;
    for (uint32_t i = 0; i < STREAMS; i++)
        if (!streams[i].active) {
            stream = &streams[i];
            break;
        }
    if (!stream)
        goto done;
    if (!device_open) {
        if (open_output() < 0)
            goto done;
        reset_output_timeline(0);
    }
    if (failed)
        goto done;
    stream->owner = owner;
    stream->rate = format->sample_rate;
    stream->channels = format->channels;
    stream->format = format->sample_format;
    /* Format enum values deliberately equal the sample width in bytes. */
    stream->bytes_per_frame = format->channels * format->sample_format;
    stream->head = stream->count = stream->phase = 0;
    stream->last_frame = 0;
    stream->draining = 0;
    stream->active = 1;
    result = 0;
done:
    mutex_unlock(&mixer_mutex);
    return result;
}
static int16_t decode(const uint8_t* p, uint32_t format) {
    if (format == USER_AUDIO_U8)
        return ((int32_t)*p - 128) * 256;
    if (format == USER_AUDIO_S16_LE)
        return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
    if (format == USER_AUDIO_S24_LE)
        return (int16_t)((uint16_t)p[1] | ((uint16_t)p[2] << 8));
    return (int16_t)((uint16_t)p[2] | ((uint16_t)p[3] << 8));
}
int32_t audio_mixer_write(uint32_t owner, const void* data, uint32_t length) {
    MUTEX_GUARD(mixer_mutex);
    mixer_stream_t* s = find(owner);
    if (!s || failed || s->draining || !data || !length || length > AUDIO_BUFFER_CHUNK_SIZE ||
        length % s->bytes_per_frame)
        return -1;
    uint32_t count = length / s->bytes_per_frame;
    if (count > QUEUE_FRAMES - s->count)
        count = QUEUE_FRAMES - s->count;
    const uint8_t* input = data;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t tail = (s->head + s->count + i) % QUEUE_FRAMES;
        s->samples[tail][0] = decode(input, s->format);
        s->samples[tail][1] =
            s->channels == 1 ? s->samples[tail][0] : decode(input + s->format, s->format);
        input += s->bytes_per_frame;
    }
    s->count += count;
    return count * s->bytes_per_frame;
}
static int32_t interpolate(int32_t a, int32_t b, uint32_t phase) {
    int32_t delta = b - a;
    uint32_t magnitude = delta < 0 ? (uint32_t)-delta : (uint32_t)delta;
    int32_t amount = (int32_t)(magnitude * phase / output_rate);
    return a + (delta < 0 ? -amount : amount);
}
static int mix_block(void) {
    int contributed = 0;
    for (uint32_t n = 0; n < BLOCK_FRAMES; n++) {
        int32_t left = 0, right = 0;
        for (uint32_t i = 0; i < STREAMS; i++) {
            mixer_stream_t* s = &streams[i];
            if (!s->active || !s->count)
                continue;
            uint32_t phase = s->phase + s->rate, advance = phase / output_rate;
            if (advance > s->count && !s->draining)
                continue;
            uint32_t next = s->count > 1 ? (s->head + 1) % QUEUE_FRAMES : s->head;
            left += interpolate(s->samples[s->head][0], s->samples[next][0], s->phase);
            right += interpolate(s->samples[s->head][1], s->samples[next][1], s->phase);
            contributed = 1;
            s->last_frame = submitted_frames + BLOCK_FRAMES;
            s->phase = phase % output_rate;
            if (advance > s->count)
                advance = s->count;
            s->head = (s->head + advance) % QUEUE_FRAMES;
            s->count -= advance;
            if (!s->count && s->draining)
                s->phase = 0;
        }
        if (left > 32767)
            left = 32767;
        if (left < -32768)
            left = -32768;
        if (right > 32767)
            right = 32767;
        if (right < -32768)
            right = -32768;
        output[n * 2] = left;
        output[n * 2 + 1] = right;
    }
    return contributed;
}
int32_t audio_mixer_status(uint32_t owner) {
    MUTEX_GUARD(mixer_mutex);
    mixer_stream_t* s = find(owner);
    if (!s || failed || !device_open)
        return -1;
    int32_t queued = audio_pending_frames();
    if (queued < 0)
        return -1;
    uint64_t played =
        submitted_frames >= (uint32_t)queued ? submitted_frames - (uint32_t)queued : 0;
    return s->count || s->last_frame > played;
}
int32_t audio_mixer_drain(uint32_t owner) {
    MUTEX_GUARD(mixer_mutex);
    mixer_stream_t* s = find(owner);
    if (!s || failed)
        return -1;
    s->draining = 1;
    return 0;
}
void audio_mixer_close(uint32_t owner) {
    MUTEX_GUARD(mixer_mutex);
    mixer_stream_t* s = find(owner);
    if (!s)
        return;
    s->active = 0;
    s->count = 0;
    if (!any_streams()) {
        if (device_open)
            audio_shutdown();
        device_open = failed = 0;
        pending_bytes = pending_offset = 0;
        submitted_frames = 0;
    }
}
void audio_mixer_service(void) {
    MUTEX_GUARD(mixer_mutex);
    audio_service();
    if (!device_open || failed)
        return;
    /* Bound work and hardware lookahead so newly opened streams join promptly. */
    for (uint32_t budget = 0; budget < 4; budget++) {
        if (pending_offset < pending_bytes) {
            int32_t accepted = audio_write((const uint8_t*)output + pending_offset,
                                           pending_bytes - pending_offset);
            if (accepted < 0 || (uint32_t)accepted > pending_bytes - pending_offset ||
                (accepted & 3)) {
                failed = 1;
                audio_shutdown();
                device_open = 0;
                return;
            }
            if (!accepted)
                return;
            pending_offset += accepted;
            submitted_frames += (uint32_t)accepted / 4;
            if (pending_offset < pending_bytes)
                continue;
        }
        int32_t queued = audio_pending_frames();
        if (queued < 0) {
            failed = 1;
            audio_shutdown();
            device_open = 0;
            return;
        }
        if ((uint32_t)queued >= AHEAD_FRAMES)
            return;
        if (!mix_block())
            return;
        pending_bytes = sizeof(output);
        pending_offset = 0;
    }
}
int32_t audio_mixer_device_select(uint32_t index) {
    MUTEX_GUARD(kernel_mutex);
    mutex_lock(&mixer_mutex);
    int32_t result = -1;
    uint32_t previous = audio_device_selected(), previous_rate = output_rate;
    if (index >= audio_device_count())
        goto done;
    if (index == previous) {
        result = 0;
        goto done;
    }
    int reopen = device_open || any_streams();
    if (device_open)
        audio_shutdown();
    device_open = 0;
    if (audio_device_select(index) == 0 && (!reopen || open_output() == 0))
        result = 0;
    else {
        audio_device_select(previous);
        if (reopen && open_output() < 0)
            failed = 1;
    }
    reset_output_timeline(previous_rate);
done:
    mutex_unlock(&mixer_mutex);
    return result;
}
