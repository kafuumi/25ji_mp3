#include <math.h>

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "hal/gpio_types.h"

#include "bsp.h"

#define BSP_MUTE_LEVEL 0
#define BSP_UNMUTE_LEVEL 1

static const char *TAG = "bsp";

typedef struct {
    adc_oneshot_unit_handle_t adc_unit;
    TaskHandle_t task;
} plustor_btn_t;

typedef struct {
    volume_change_handler cb;
    int min_val;
    int internval;
} plustor_btn_task_ctx_t;

static plustor_btn_t g_plustor_btn = {0};

static void bsp_button_plustor_read_task(void *args) {
    plustor_btn_task_ctx_t *task_ctx = args;
    volume_change_handler handler = task_ctx->cb;
    int value, last_val;
    adc_oneshot_read(g_plustor_btn.adc_unit, BSP_PIN_BTN_PLUSTOR_ADC_CHAN, &value);
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(task_ctx->internval));
        last_val = value;
        adc_oneshot_read(g_plustor_btn.adc_unit, BSP_PIN_BTN_PLUSTOR_ADC_CHAN, &value);
        int diff = value - last_val;
        if (abs(diff) >= task_ctx->min_val) {
            handler(value, diff);
        }
    }
}

static esp_err_t bsp_button_init() {
    if (g_plustor_btn.adc_unit && g_plustor_btn.task) {
        return ESP_OK;
    }
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT_1,
        .clk_src = ADC_RTC_CLK_SRC_DEFAULT,
    };
    esp_err_t ret = ESP_OK;

    adc_oneshot_unit_handle_t adc_unit;
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&unit_cfg, &adc_unit), TAG, "init adc unit 1 fail: %d", ret);

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_GOTO_ON_ERROR(adc_oneshot_config_channel(adc_unit, BSP_PIN_BTN_PLUSTOR_ADC_CHAN, &chan_cfg), _cleanup, TAG,
                      "config adc channel fail: %d", ret);

    g_plustor_btn.adc_unit = adc_unit;
    return ESP_OK;

_cleanup:
    if (adc_unit) {
        adc_oneshot_del_unit(adc_unit);
    }
    return ESP_OK;
}

#define BSP_BTN_PLUSTOR_TASK_NAME "bsp_plustor_task"
#define BSP_BTN_PLUSTOR_TASK_SIZE 4096
#define BSP_BTN_PLUSTOR_TASK_PRIORITY 10
#define BSP_BTN_PLUSTOR_TASK_CPU_NUM APP_CPU_NUM

esp_err_t bsp_btn_plustor_registor_cb(volume_change_handler cb, int min_val, int internal) {
    if (!g_plustor_btn.adc_unit) {
        ESP_LOGI(TAG, "plustor button not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    if (g_plustor_btn.task) {
        ESP_LOGE(TAG, "plustor handler already registered");
        return ESP_ERR_INVALID_STATE;
    }
    plustor_btn_task_ctx_t *task_ctx = malloc(sizeof(plustor_btn_task_ctx_t));
    task_ctx->cb = cb;
    task_ctx->min_val = min_val;
    task_ctx->internval = internal;
    TaskHandle_t task;
    if (xTaskCreatePinnedToCore(bsp_button_plustor_read_task, BSP_BTN_PLUSTOR_TASK_NAME, BSP_BTN_PLUSTOR_TASK_SIZE,
                                task_ctx, BSP_BTN_PLUSTOR_TASK_PRIORITY, &task,
                                BSP_BTN_PLUSTOR_TASK_CPU_NUM) != pdTRUE) {
        ESP_LOGE(TAG, "create plustor read task fail");
        goto _cleanup;
    }
    g_plustor_btn.task = task;
    return ESP_OK;

_cleanup:
    if (task_ctx) {
        free(task);
    }
    g_plustor_btn.task = NULL;
    return ESP_FAIL;
}

esp_err_t bsp_init() {
    esp_err_t err;

    err = gpio_set_direction(BSP_PIN_I2S_MUTE, GPIO_MODE_OUTPUT);
    ESP_RETURN_ON_ERROR(err, TAG, "set mute gpio ping direction fail: %d(%s)", err, esp_err_to_name(err));

    ESP_RETURN_ON_ERROR(bsp_button_init(), TAG, "init button fail");

    err = bsp_sd_card_mount();
    if (ESP_OK != err) {
        ESP_LOGW(TAG, "mount sd card fail");
    }
    return ESP_OK;
}

esp_err_t bsp_audio_mute(bool mute) {
    if (mute) {
        gpio_set_level(BSP_PIN_I2S_MUTE, BSP_MUTE_LEVEL);
    } else {
        gpio_set_level(BSP_PIN_I2S_MUTE, BSP_UNMUTE_LEVEL);
    }
    return ESP_OK;
}
