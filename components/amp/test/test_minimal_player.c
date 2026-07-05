
#include "unity.h"

#include "amp/audio_decoder.h"
#include "amp/controller.h"
#include "amp/devnull_writer.h"
#include "amp/file_reader.h"
#include "amp/i2s_writer.h"
#include "bsp.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "minimal_player";

static amp_controller_handle_t g_amp_controller = NULL;

static void print_heap(const char *tag) {
    ESP_LOGW(tag, "free=%u min_free=%u largest=%u", heap_caps_get_free_size(MALLOC_CAP_8BIT),
             heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

#define DEFAULT_ELEMENT_TASK_CFG()                                                                                     \
    {                                                                                                                  \
        .output_rb_size = 1024,                                                                                        \
        .stack_size = 4096,                                                                                            \
        .affinity_core = PRO_CPU_NUM,                                                                                  \
        .task_priority = 10,                                                                                           \
    }

static esp_err_t create_player() {
    esp_err_t err;

    amp_playlist_handle_t playlist;
    amp_playlist_cfg_t pl_cfg = {
        .base_dir = "/storage/music",
        .recursion = true,
    };
    err = amp_playlist_init(&pl_cfg, &playlist);
    ESP_RETURN_ON_ERROR(err, TAG, "create amp playlist fail: %d", err);

    amp_file_reader_handle_t file_reader;
    amp_file_reader_cfg_t fr_cfg = {
        .playlist = playlist,
    };
    err = amp_file_reader_init(&fr_cfg, &file_reader);
    ESP_RETURN_ON_ERROR(err, TAG, "create amp file reader fail: %d", err);

    amp_audio_decoder_handle_t decoder;
    err = amp_audio_decoder_init(&decoder);
    ESP_RETURN_ON_ERROR(err, TAG, "create amp audio codec fail: %d", err);

    amp_i2s_writer_handle_t i2s_writer;
    amp_i2s_writer_cfg_t iw_cfg = {
        .i2s_port = I2S_NUM_0,
        .volume = 50,
        .gpio_cfg =
            {
                .bclk = BSP_PIN_I2S_BCK,
                .mclk = BSP_PIN_I2S_MCK,
                .dout = BSP_PIN_I2S_DOUT,
                .ws = BSP_PIN_I2S_WS,
            },
    };
    err = amp_i2s_writer_init(&iw_cfg, &i2s_writer);
    ESP_RETURN_ON_ERROR(err, TAG, "create amp i2s writer fail: %d", err);

    amp_controller_handle_t controller;
    err = amp_controller_init(&controller);
    ESP_RETURN_ON_ERROR(err, TAG, "create amp controller fail: %d", err);

    amp_element_task_config_t task_cfg = DEFAULT_ELEMENT_TASK_CFG();
    task_cfg.intf = amp_file_reader_get_element_interface();
    task_cfg.name = "file_reader";
    err = amp_controller_append_reader(controller, (amp_element_handle_t)file_reader, &task_cfg);
    ESP_RETURN_ON_ERROR(err, TAG, "append file reader fail: %d", err);

    task_cfg.intf = amp_audio_decoder_get_element_interface();
    task_cfg.name = "audio_decoder";
    err = amp_controller_append_processor(controller, (amp_element_handle_t)decoder, &task_cfg);
    ESP_RETURN_ON_ERROR(err, TAG, "append audio decoder fail: %d", err);

    task_cfg.intf = amp_i2s_writer_get_element_interface();
    task_cfg.name = "i2s_writer";
    err = amp_controller_append_writer(controller, (amp_element_handle_t)i2s_writer, &task_cfg);
    ESP_RETURN_ON_ERROR(err, TAG, "append i2s writer fail: %d", err);

    err = amp_controller_run(controller);
    ESP_RETURN_ON_ERROR(err, TAG, "run amp element fail: %d", err);

    g_amp_controller = controller;
    return err;
}

TEST_CASE("minimal player", "[amp]") {
    esp_err_t err = create_player();
    TEST_ASSERT_EQUAL(ESP_OK, err);

    amp_controller_action_play(g_amp_controller);
    while (true) {

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
