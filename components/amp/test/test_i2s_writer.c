#include <math.h>
#include <string.h>

#include "esp_cpu.h"
#include "esp_log.h"
#include "unity.h"

#include "amp/controller.h"
#include "amp/i2s_writer.h"
#include "amp/sin_pcm_reader.h"

#define ELEMENT_CREATE(init_func, obj)                                                                                 \
    {                                                                                                                  \
        init_func;                                                                                                     \
        TEST_ASSERT_EQUAL(ESP_OK, err);                                                                                \
        TEST_ASSERT_NOT_NULL(obj);                                                                                     \
    }

static const char *TAG = "i2s_writer_test";

#define VOLUME_Q_FORMAT 14
#define VOLUME_CHANGE_RANGE 60

static amp_sine_pcm_reader_handle_t create_sin_pcm_reader() {
    amp_sine_pcm_reader_handle_t reader;
    amp_sine_pcm_reader_cfg_t cfg = {
        .frames_size = 512,
        .max_amplitude = 3000,
    };
    esp_err_t err;
    ELEMENT_CREATE(err = amp_sine_pcm_reader_init(&cfg, &reader);, reader);

    amp_sine_pcm_audio_config_t audio_cfg = {
        .bit_width = AUDIO_BIT_WIDTH_16BIT,
        .channel = AUDIO_CHANNEL_STEREO,
        .freq = 440,
        .sample_rate = 44100,
        .volume = 100,
    };
    amp_sine_pcm_reader_set_audio_config(reader, &audio_cfg);
    return reader;
}

static amp_i2s_writer_handle_t create_i2s_writer(uint8_t volume) {
    amp_i2s_writer_handle_t writer;
    amp_i2s_writer_cfg_t cfg = {
        .i2s_port = I2S_NUM_0,
        .volume = volume,
    };
    esp_err_t err;
    ELEMENT_CREATE(err = amp_i2s_writer_init(&cfg, &writer), writer);
    return writer;
}

static amp_controller_handle_t create_controller() {
    amp_controller_handle_t controller;
    esp_err_t err;
    ELEMENT_CREATE(err = amp_controller_init(&controller), controller);
    return controller;
}

TEST_CASE("volume set to 20", "[amp][i2s_writer]") {
    amp_sine_pcm_reader_handle_t reader = create_sin_pcm_reader();
    amp_i2s_writer_handle_t writer = create_i2s_writer(20);
    amp_controller_handle_t controller = create_controller();

    amp_element_task_config_t reader_task_cfg = {
        .intf = amp_sine_pcm_reader_get_element_interface(),
        .name = "reader",
        .output_rb_size = 2048,
        .stack_size = 4096,
    };
    amp_controller_append_reader(controller, (amp_element_handle_t)reader, &reader_task_cfg);

    amp_element_task_config_t writer_task_cfg = {
        .intf = amp_i2s_writer_get_element_interface(),
        .name = "writer",
        .output_rb_size = 1024,
        .stack_size = 4096,
    };
    amp_controller_append_writer(controller, (amp_element_handle_t)writer, &writer_task_cfg);

    amp_controller_run(controller);
    amp_controller_action_play(controller);
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static int32_t get_q14_gain_from_volume(uint8_t volume) {
    if (volume == 100) {
        return 1 << VOLUME_Q_FORMAT;
    }
    if (volume == 0) {
        return 0;
    }

    float y = powf((float)volume / 100.0f, 2.0f);
    return lrintf(powf(10.0f, (-VOLUME_CHANGE_RANGE + y * VOLUME_CHANGE_RANGE) / 20.0f) * (1 << VOLUME_Q_FORMAT));
}

static float get_float_gain_from_volume(uint8_t volume) {
    if (volume == 100) {
        return 1.0f;
    }
    if (volume == 0) {
        return 0.0f;
    }

    float y = powf((float)volume / 100.0f, 2.0f);
    return powf(10.0f, (-VOLUME_CHANGE_RANGE + y * VOLUME_CHANGE_RANGE) / 20.0f);
}

static void bench_qformat_volume(int32_t *nums, int size, int32_t gain_q14) {
    int64_t gain = gain_q14;
    for (int i = 0; i < size; ++i) {
        nums[i] = (int32_t)((nums[i] * gain) >> VOLUME_Q_FORMAT);
    }
}

static void bench_float_volume(int32_t *nums, int size, float gain_f32) {
    for (int i = 0; i < size; ++i) {
        nums[i] = (int32_t)(nums[i] * gain_f32);
    }
}

TEST_CASE("bench q-format vs float volume", "[amp][bench]") {
    const int size = 1024;
    const uint32_t loops = 10000;
    int32_t *orig = malloc(sizeof(int32_t) * size);
    int32_t *nums = malloc(sizeof(int32_t) * size);

    for (int i = 0; i < size; ++i) {
        orig[i] = i * 100;
    }
    uint8_t volume = 33;
    int32_t gain_q14 = get_q14_gain_from_volume(volume);
    float gain_f32 = get_float_gain_from_volume(volume);

    memcpy(nums, orig, sizeof(int32_t) * size);
    uint32_t start = esp_cpu_get_cycle_count();
    for (int i = 0; i < loops; i++) {
        bench_qformat_volume(nums, size, gain_q14);
    }
    uint32_t end = esp_cpu_get_cycle_count();
    uint32_t ret_qformat = (float)(end - start) / loops;

    memcpy(nums, orig, sizeof(int32_t) * size);
    start = esp_cpu_get_cycle_count();
    for (int i = 0; i < loops; i++) {
        bench_float_volume(nums, size, gain_f32);
    }
    end = esp_cpu_get_cycle_count();
    uint32_t ret_float = (float)(end - start) / loops;

    ESP_LOGI(TAG, "volume %u: qformat=%ld cycle, float=%ld cycle", volume, ret_qformat, ret_float);
    free(orig);
    free(nums);
}
