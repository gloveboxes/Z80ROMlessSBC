#pragma once

#include <stdint.h>

typedef void (*speaker_audio_render_fn)(float *output, uint32_t frames);

/* Opens and starts a mono float playback device. Returns 0 when audio is
 * unavailable or not compiled in. The callback runs render under the lock. */
int speaker_audio_open(uint32_t sample_rate, speaker_audio_render_fn render);
void speaker_audio_close(void);
void speaker_audio_lock(void);
void speaker_audio_unlock(void);
