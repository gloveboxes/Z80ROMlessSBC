#include "PortDrivers/speaker_audio.h"

#include <stddef.h>

#ifdef Z80SBC_SPEAKER_AUDIO
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

static ma_device s_device;
static ma_mutex s_lock;
static speaker_audio_render_fn s_render;
static int s_open;

static void audio_callback(ma_device *device, void *output, const void *input,
                           ma_uint32 frames)
{
    (void)device;
    (void)input;
    ma_mutex_lock(&s_lock);
    s_render((float *)output, frames);
    ma_mutex_unlock(&s_lock);
}

int speaker_audio_open(uint32_t sample_rate, speaker_audio_render_fn render)
{
    ma_device_config config;

    if (s_open || ma_mutex_init(&s_lock) != MA_SUCCESS)
        return s_open;
    config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = 1;
    config.sampleRate = sample_rate;
    config.dataCallback = audio_callback;
    s_render = render;
    if (ma_device_init(NULL, &config, &s_device) != MA_SUCCESS) {
        ma_mutex_uninit(&s_lock);
        return 0;
    }
    /* Set before starting: the Z80 thread must lock once callbacks can run. */
    s_open = 1;
    if (ma_device_start(&s_device) != MA_SUCCESS) {
        s_open = 0;
        ma_device_uninit(&s_device);
        ma_mutex_uninit(&s_lock);
        return 0;
    }
    return 1;
}

void speaker_audio_close(void)
{
    if (!s_open)
        return;
    ma_device_uninit(&s_device);
    ma_mutex_uninit(&s_lock);
    s_open = 0;
}

void speaker_audio_lock(void)
{
    if (s_open)
        ma_mutex_lock(&s_lock);
}

void speaker_audio_unlock(void)
{
    if (s_open)
        ma_mutex_unlock(&s_lock);
}
#else
int speaker_audio_open(uint32_t sample_rate, speaker_audio_render_fn render)
{
    (void)sample_rate;
    (void)render;
    return 0;
}

void speaker_audio_close(void) {}
void speaker_audio_lock(void) {}
void speaker_audio_unlock(void) {}
#endif
