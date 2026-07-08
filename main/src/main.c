#include "button_gpio.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_err.h"
#include "iot_button.h"

#include "amp/audio_decoder.h"
#include "amp/controller.h"
#include "amp/file_reader.h"
#include "amp/i2s_writer.h"
#include "amp/playlist.h"
#include "bsp.h"
#include "sensor/aht20.h"
#include "ui.h"

#include "utils/debug_utils.h"

#define DEFAULT_PLAYLIST_DIR BSP_SD_CARD_MOUNT_POINT "/music"

#define AMP_TASK_CPU_CORE PRO_CPU_NUM
#define AMP_TASK_DEFAULT_PRIORITY 15

static const char *TAG = "app";

typedef struct {
    amp_controller_handle_t controller;
    amp_playlist_handle_t playlist;
    amp_file_reader_handle_t file_reader;
    amp_audio_decoder_handle_t audio_codec;
    amp_i2s_writer_handle_t i2s_writer;
} amp_player_ctx_t;

static amp_player_ctx_t *g_amp_player = NULL;
static i2c_master_bus_handle_t g_i2c_bus = NULL;
static TaskHandle_t g_sensor_task = NULL;
static bool g_flag_playing = false;
static button_handle_t *g_button_list = NULL;

//////////////////////////////////////////////////////////////////////

#define DEFAULT_ELEMENT_TASK_CFG()                                                                                     \
    {                                                                                                                  \
        .output_rb_size = 1024,                                                                                        \
        .stack_size = 4096,                                                                                            \
        .affinity_core = AMP_TASK_CPU_CORE,                                                                            \
        .task_priority = AMP_TASK_DEFAULT_PRIORITY,                                                                    \
    }

static esp_err_t amp_player_init() {
    // set to mute
    bsp_audio_mute(true);
    if (g_amp_player) {
        return ESP_OK;
    }

    amp_player_ctx_t *player = malloc(sizeof(amp_player_ctx_t));
    if (!player) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err;

    amp_playlist_handle_t playlist;
    amp_playlist_cfg_t pl_cfg = {
        .base_dir = DEFAULT_PLAYLIST_DIR,
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

    task_cfg.intf = amp_i2s_writer_get_element_interface();
    task_cfg.name = "i2s_writer";
    err = amp_controller_append_writer(controller, (amp_element_handle_t)i2s_writer, &task_cfg);
    ESP_RETURN_ON_ERROR(err, TAG, "append i2s writer fail: %d", err);

    err = amp_controller_run(controller);
    ESP_RETURN_ON_ERROR(err, TAG, "run amp element fail: %d", err);

    amp_playlist_preload(playlist);

    player->controller = controller;
    player->playlist = playlist;
    player->file_reader = file_reader;
    player->audio_codec = decoder;
    player->i2s_writer = i2s_writer;

    g_amp_player = player;
    return ESP_OK;
}

//////////////////////////////////////////////////////////////////////

#define APP_I2C_PORT I2C_NUM_0

static esp_err_t i2c_bus_init() {
    if (g_i2c_bus) {
        return ESP_OK;
    }
    const i2c_master_bus_config_t i2c_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .flags = {.enable_internal_pullup = false},
        .i2c_port = APP_I2C_PORT,
        .scl_io_num = BSP_PIN_I2C_SCL,
        .sda_io_num = BSP_PIN_I2C_SDA,
    };
    i2c_master_bus_handle_t i2c_bus;
    esp_err_t err = i2c_new_master_bus(&i2c_cfg, &i2c_bus);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "new i2c master bus fail: %d(%s)", err, esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "success to initialize i2c bus, sda: %d, scl: %d", i2c_cfg.sda_io_num, i2c_cfg.scl_io_num);
    g_i2c_bus = i2c_bus;
    return ESP_OK;
}

//////////////////////////////////////////////////////////////////////

#define SENSOR_AHT20_READ_DELAY pdMS_TO_TICKS(5000)
#define SENSOR_AHT20_TASK_CPU_NUM APP_CPU_NUM
#define SENSOR_AHT20_TASK_PRIORITY 10
#define SENSOR_AHT20_TASK_SIZE 4096

static void sensor_read_task(void *args) {
    float temp, humi;
    esp_err_t err;
    while (true) {
        err = aht20_read_temperature_humidity(&temp, &humi);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "read sensor fail: %s", esp_err_to_name(err));
            continue;
        }
        ESP_LOGD(TAG, "read sensor: %.2f, %.2f", temp, humi);
        vTaskDelay(SENSOR_AHT20_READ_DELAY);
    }
}

///////////////////////////////////////////////////////////////

#define GPIO_BTN_DEFAULT_CFG(io_num)                                                                                   \
    {                                                                                                                  \
        .gpio_num = io_num,                                                                                            \
        .active_level = BSP_PIN_BTN_ACTIVE_LEVEL,                                                                      \
        .disable_pull = false,                                                                                         \
        .enable_power_save = true,                                                                                     \
    }

static void handle_volume_change_event(int volume, int diff) {
    ESP_LOGI(TAG, "volume changed, current: %d, diff: %d", volume, diff);
    if (g_amp_player) {
        amp_i2s_writer_set_volume(g_amp_player->i2s_writer, volume);
    }
}

static void next_btn_single_click_cb(void *args, void *user_data) {
    ui_post_input(UI_INPUT_NEXT);
    amp_controller_action_next(g_amp_player->controller);
}

static void prev_btn_single_click_cb(void *args, void *user_data) { ui_post_input(UI_INPUT_PREV); }

static void any_btn_single_click_cb(void *args, void *user_data) {
    ESP_LOGI(TAG, "any button clicked");
    if (g_flag_playing) {
        bsp_audio_mute(true);
        amp_controller_action_pause(g_amp_player->controller);
    } else {
        bsp_audio_mute(false);
        amp_controller_action_play(g_amp_player->controller);
    }
    g_flag_playing = !g_flag_playing;
}

static void button_deinit() {
    if (!g_button_list) {
        return;
    }
    button_handle_t *btn_list = g_button_list;
    while (*btn_list) {
        iot_button_delete(*btn_list);
        ++btn_list;
    }
    free(g_button_list);
}

static esp_err_t button_init() {
    if (g_button_list) {
        return ESP_OK;
    }
    int btn_size = 3;
    g_button_list = malloc(sizeof(button_handle_t) * (btn_size + 1));
    if (!g_button_list) {
        return ESP_ERR_NO_MEM;
    }
    g_button_list[btn_size] = NULL; // end flag

    button_handle_t *btn_list = g_button_list;
    const button_config_t btn_cfg = {0};
    esp_err_t ret = ESP_OK;

    const button_gpio_config_t prev_btn_cfg = GPIO_BTN_DEFAULT_CFG(BSP_PIN_BTN_PREV);
    button_handle_t prev_btn;
    ESP_GOTO_ON_ERROR(iot_button_new_gpio_device(&btn_cfg, &prev_btn_cfg, &prev_btn), _cleanup, TAG,
                      "new prev gpio button fail: %d", ret);
    *btn_list = prev_btn;
    btn_list++;

    const button_gpio_config_t next_btn_cfg = GPIO_BTN_DEFAULT_CFG(BSP_PIN_BTN_NEXT);
    button_handle_t next_btn;
    ESP_GOTO_ON_ERROR(iot_button_new_gpio_device(&btn_cfg, &next_btn_cfg, &next_btn), _cleanup, TAG,
                      "new next gpio button fail: %d", ret);
    *btn_list = next_btn;
    btn_list++;

    const button_gpio_config_t any_btn_cfg = GPIO_BTN_DEFAULT_CFG(BSP_PIN_BTN_ANY);
    button_handle_t any_btn;
    ESP_GOTO_ON_ERROR(iot_button_new_gpio_device(&btn_cfg, &any_btn_cfg, &any_btn), _cleanup, TAG,
                      "new any gpio button fail: %d", ret);
    *btn_list = any_btn;
    btn_list++;

    ESP_GOTO_ON_ERROR(iot_button_register_cb(any_btn, BUTTON_SINGLE_CLICK, NULL, any_btn_single_click_cb, NULL),
                      _cleanup, TAG, "any btn register SINGLE_CLICK event fail: %d", ret);
    ESP_GOTO_ON_ERROR(iot_button_register_cb(prev_btn, BUTTON_SINGLE_CLICK, NULL, prev_btn_single_click_cb, NULL),
                      _cleanup, TAG, "prev btn register SINGLE_CLICK event fail: %d", ret);
    ESP_GOTO_ON_ERROR(iot_button_register_cb(next_btn, BUTTON_SINGLE_CLICK, NULL, next_btn_single_click_cb, NULL),
                      _cleanup, TAG, "next btn register SINGLE_CLICK event fail: %d", ret);

    ESP_GOTO_ON_ERROR(bsp_btn_plustor_register_cb(handle_volume_change_event, 2, 50), _cleanup, TAG,
                      "register plustor button event fail: %d", ret);

    return ESP_OK;
_cleanup:
    button_deinit();
    return ret;
}

void app_main(void) {
    ESP_LOGI(TAG, "25ji mp3 start...");
    ESP_ERROR_CHECK(i2c_bus_init());
    ESP_ERROR_CHECK(ui_init(g_i2c_bus));
    ESP_ERROR_CHECK(bsp_init());
    ESP_ERROR_CHECK(amp_player_init());
    ESP_ERROR_CHECK(button_init());
    ESP_ERROR_CHECK(aht20_init(g_i2c_bus));

    ui_cfg_t ui_cfg = {
        .task_cpu_num = APP_CPU_NUM,
        .task_name = "ui_draw",
        .task_priority = 5,
        .task_size = 4096,
    };
    ESP_ERROR_CHECK(ui_start(&ui_cfg));

    BaseType_t ret;
    TaskHandle_t sensor_task;
    ret = xTaskCreatePinnedToCore(sensor_read_task, "sensor", SENSOR_AHT20_TASK_SIZE, NULL, SENSOR_AHT20_TASK_PRIORITY,
                                  &sensor_task, SENSOR_AHT20_TASK_CPU_NUM);
    if (ret != pdTRUE) {
        ESP_LOGE(TAG, "create sensor task fail");
        abort();
    }
    g_sensor_task = sensor_task;
    // start_print_heap_task();
    ESP_LOGI(TAG, "app start finished, enjoy!");
}
