#ifndef DAW_PLUGINS_H
#define DAW_PLUGINS_H

#include <math.h>
#include <string.h>
#include <stdbool.h>

#define DELAY_MAX_SAMPLES 96000 // 2 seconds at 48kHz stereo

typedef struct {
    bool enabled;
    float cutoff_hz; // 20Hz - 20000Hz
    float prev_left;
    float prev_right;
} SimpleFilterPlugin;

typedef struct {
    bool enabled;
    float delay_time_sec;
    float feedback; // 0.0 - 0.95
    float buffer[DELAY_MAX_SAMPLES * 2];
    int write_pos;
} SimpleDelayPlugin;

// Soft saturation curve plugin (transparent mastering ceiling)
static inline float dsp_soft_clip(float x) {
    if (x > 1.0f) return 1.0f - expf(-x);
    if (x < -1.0f) return -1.0f + expf(x);
    return x;
}

// In-place processing
static inline void process_master_plugins(float *stereo_buffer, int frames, SimpleFilterPlugin *filt, SimpleDelayPlugin *del) {
    // 1. Filter Pass
    if (filt && filt->enabled) {
        float rc = 1.0f / (2.0f * 3.14159265f * filt->cutoff_hz);
        float dt = 1.0f / 48000.0f;
        float alpha = dt / (rc + dt);

        for (int i = 0; i < frames; i++) {
            filt->prev_left += alpha * (stereo_buffer[i * 2] - filt->prev_left);
            filt->prev_right += alpha * (stereo_buffer[i * 2 + 1] - filt->prev_right);
            stereo_buffer[i * 2] = filt->prev_left;
            stereo_buffer[i * 2 + 1] = filt->prev_right;
        }
    }

    // 2. Stereo Delay Pass
    if (del && del->enabled) {
        int delay_samples = (int)(del->delay_time_sec * 48000.0f);
        if (delay_samples >= DELAY_MAX_SAMPLES) delay_samples = DELAY_MAX_SAMPLES - 1;

        for (int i = 0; i < frames; i++) {
            int read_pos = (del->write_pos - delay_samples + DELAY_MAX_SAMPLES) % DELAY_MAX_SAMPLES;
            float dl = del->buffer[read_pos * 2];
            float dr = del->buffer[read_pos * 2 + 1];

            del->buffer[del->write_pos * 2] = stereo_buffer[i * 2] + dl * del->feedback;
            del->buffer[del->write_pos * 2 + 1] = stereo_buffer[i * 2 + 1] + dr * del->feedback;

            stereo_buffer[i * 2] += dl * 0.5f;
            stereo_buffer[i * 2 + 1] += dr * 0.5f;

            del->write_pos = (del->write_pos + 1) % DELAY_MAX_SAMPLES;
        }
    }

    // 3. Peak Limiter / Soft Clipper
    for (int i = 0; i < frames * 2; i++) {
        stereo_buffer[i] = dsp_soft_clip(stereo_buffer[i]);
    }
}

#endif // DAW_PLUGINS_H
