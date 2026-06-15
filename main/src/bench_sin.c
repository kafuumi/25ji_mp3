#include "esp_log.h"
#include "esp_timer.h"
#include <math.h>

static volatile int32_t bench_sink;
static const char *TAG = "bench_sin";

static int64_t bench_generate_with_sin(int16_t *buf, size_t frames, int freq, int sample_rate) {
    float phase = 0.0f;
    const float amplitude = 8000.0f;
    const float two_pi = 2.0f * (float)M_PI;
    const float delta = two_pi * freq / sample_rate;

    int64_t start = esp_timer_get_time();

    for (size_t i = 0; i < frames; i++) {
        phase += delta;
        if (phase >= two_pi) {
            phase -= two_pi;
        }

        float value = sin(phase); // double sin(double)
        int16_t sample = (int16_t)lrintf(value * amplitude);

        buf[i * 2] = sample;
        buf[i * 2 + 1] = sample;
    }

    int64_t end = esp_timer_get_time();

    int32_t sum = 0;
    for (size_t i = 0; i < frames * 2; i++) {
        sum += buf[i];
    }
    bench_sink = sum;

    return end - start;
}

static int64_t bench_generate_with_sinf(int16_t *buf, size_t frames, int freq, int sample_rate) {
    float phase = 0.0f;
    const float amplitude = 8000.0f;
    const float two_pi = 2.0f * (float)M_PI;
    const float delta = two_pi * freq / sample_rate;

    int64_t start = esp_timer_get_time();

    for (size_t i = 0; i < frames; i++) {
        phase += delta;
        if (phase >= two_pi) {
            phase -= two_pi;
        }

        float value = sinf(phase); // float sinf(float)
        int16_t sample = (int16_t)lrintf(value * amplitude);

        buf[i * 2] = sample;
        buf[i * 2 + 1] = sample;
    }

    int64_t end = esp_timer_get_time();

    int32_t sum = 0;
    for (size_t i = 0; i < frames * 2; i++) {
        sum += buf[i];
    }
    bench_sink = sum;

    return end - start;
}

void bench_sin_vs_sinf(void) {
    const size_t frames = 2048;
    const int freq = 440;
    const int sample_rate = 48000;
    const int rounds = 100;

    int16_t *buf = malloc(sizeof(int16_t) * frames * 2);
    if (!buf) {
        ESP_LOGE(TAG, "benchmark malloc failed");
        return;
    }

    int64_t sin_total = 0;
    int64_t sinf_total = 0;

    bench_generate_with_sin(buf, frames, freq, sample_rate);
    bench_generate_with_sinf(buf, frames, freq, sample_rate);

    for (int i = 0; i < rounds; i++) {
        sin_total += bench_generate_with_sin(buf, frames, freq, sample_rate);
        sinf_total += bench_generate_with_sinf(buf, frames, freq, sample_rate);
    }

    ESP_LOGI(TAG, "sin  avg: %lld us", sin_total / rounds);
    ESP_LOGI(TAG, "sinf avg: %lld us", sinf_total / rounds);
    ESP_LOGI(TAG, "ratio: %.2fx", (double)sin_total / (double)sinf_total);

    free(buf);
}
