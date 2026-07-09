#include <stdio.h>

#include "unity.h"

#include "amp/audio_decoder.h"
#include "amp/controller.h"
#include "amp/devnull_writer.h"
#include "amp/file_reader.h"
#include "bsp.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "minimal_player";

static amp_controller_handle_t g_amp_controller = NULL;

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

    amp_devnull_writer_handle_t writer;
    err = amp_devnull_writer_init(&writer);
    ESP_RETURN_ON_ERROR(err, TAG, "create amp null writer fail: %d", err);

    amp_controller_handle_t controller;
    amp_controller_cfg_t controller_cfg = {
        .playlist = playlist,
    };
    err = amp_controller_init(&controller_cfg, &controller);
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

    task_cfg.intf = amp_devnull_writer_get_element_interface();
    task_cfg.name = "null_writer";
    err = amp_controller_append_writer(controller, (amp_element_handle_t)writer, &task_cfg);
    ESP_RETURN_ON_ERROR(err, TAG, "append null writer fail: %d", err);

    err = amp_controller_run(controller);
    ESP_RETURN_ON_ERROR(err, TAG, "run amp element fail: %d", err);

    g_amp_controller = controller;
    return err;
}

TEST_CASE("null player init and deinit", "[amp]") {
    esp_err_t err = create_player();
    TEST_ASSERT_EQUAL(ESP_OK, err);

    amp_controller_action_play(g_amp_controller);
    vTaskDelay(pdMS_TO_TICKS(3000));
    amp_controller_stop(g_amp_controller);
    amp_controller_deinit(g_amp_controller);
}

TEST_CASE("devnull player", "[amp]") {
    esp_err_t err = create_player();
    TEST_ASSERT_EQUAL(ESP_OK, err);

    amp_controller_action_play(g_amp_controller);

    while (true) {
        int input = getc(stdin);
        switch (input) {
        case 'n':
            printf("play next\n");
            amp_controller_action_next(g_amp_controller);
            break;
        case 'p':
            printf("play prev\n");
            amp_controller_action_prev(g_amp_controller);
            break;
        case 'q':
            printf("exit\n");
            amp_controller_action_pause(g_amp_controller);
            goto _test_end;
        case 's':
            printf("toggle play\n");
            amp_controller_action_toggle_play(g_amp_controller, NULL);
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
_test_end:
    amp_controller_stop(g_amp_controller);
    amp_controller_deinit(g_amp_controller);
}
