#include "ui.h"
#include "bsp.h"
#include "driver/i2c_types.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "u8g2/mui.h"
#include "u8g2/mui_u8g2.h"
#include "u8g2/u8g2.h"
#include "u8g2_port.h"

#define UI_FPS 20
#define INPUT_QUEUE_SIZE 8

/////////////////////////////////////////////

#define FORM_ID_MAIN 1

static const char *TAG = "ui";

static u8g2_t *g_u8g2_handle = NULL;
static mui_t *g_mui_handle = NULL;
static TaskHandle_t g_mui_task = NULL;
static QueueHandle_t g_mui_input_queue = NULL;

muif_t muif_list[] = {MUIF_VARIABLE("BN", NULL, mui_u8g2_btn_exit_wm_fi)};

fds_t fds_data[] = MUI_FORM(1) MUI_XYT("BN", 64, 10, " Select Me ") MUI_FORM(2) MUI_XYT("B2", 64, 40, " Select Me ");

#define UI_RENDER(_u8g2, _draw_func)                                                                                   \
    {                                                                                                                  \
        u8g2_FirstPage(_u8g2);                                                                                         \
        do {                                                                                                           \
            _draw_func                                                                                                 \
        } while (u8g2_NextPage(_u8g2));                                                                                \
    }

#define UI_FLUSH(_mui, _u8g2) UI_RENDER(_u8g2, mui_Draw(_mui);)

static void mui_draw_task(void *args) {
    mui_t *mui = g_mui_handle;
    u8g2_t *u8g2 = g_u8g2_handle;
    QueueHandle_t input_queue = g_mui_input_queue;

    bool redraw = true;
    TickType_t wait = pdMS_TO_TICKS(1000 / 30);
    ui_input_event_t input;

    while (true) {
        while (xQueueReceive(input_queue, &input, 0) == pdTRUE) {
            ESP_LOGI(TAG, "mui receive input %d", input);
            switch (input) {
            case UI_INPUT_SELECT:
                mui_SendSelect(mui);
                break;
            case UI_INPUT_NEXT:
                mui_NextField(mui);
                break;
            case UI_INPUT_PREV:
                mui_PrevField(mui);
                break;
            default:
                goto _draw_ui;
            }
            redraw = true;
        }
    _draw_ui:
        if (mui_IsFormActive(mui)) {
            if (redraw) {
                UI_FLUSH(mui, u8g2);
            }
            redraw = false;
        }
        vTaskDelay(wait);
    }
}

esp_err_t ui_post_input(ui_input_event_t event) {
    BaseType_t ret = xQueueSend(g_mui_input_queue, &event, 0);
    if (ret != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t ui_start(ui_cfg_t *cfg) {
    if (!g_u8g2_handle || !g_mui_handle) {
        return ESP_ERR_INVALID_STATE;
    }
    QueueHandle_t input_queue = xQueueCreate(INPUT_QUEUE_SIZE, sizeof(ui_input_event_t));
    if (!input_queue) {
        return ESP_ERR_NO_MEM;
    }
    g_mui_input_queue = input_queue;

    TaskHandle_t task;
    BaseType_t ret = xTaskCreatePinnedToCore(mui_draw_task, cfg->task_name, cfg->task_size, NULL, cfg->task_priority,
                                             &task, cfg->task_cpu_num);
    if (ret != pdTRUE) {
        vQueueDelete(input_queue);
        g_mui_input_queue = NULL;
        return ESP_FAIL;
    }
    g_mui_task = task;
    return ESP_OK;
}

esp_err_t ui_init(i2c_master_bus_handle_t i2c_bus_handle) {
    if (g_u8g2_handle) {
        return ESP_ERR_INVALID_STATE;
    }
    const u8g2_port_i2c_config_t u8g2_port_cfg = {
        .i2c_bus = i2c_bus_handle,
        .rotation = ROTATION_180,
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .dev_address = 0x3C,
        .scl_freq_hz = 100 * 1000,
        .buf_size = 32,
    };
    u8g2_t *u8g2 = (u8g2_t *)malloc(sizeof(u8g2_t));
    esp_err_t err = u8g2_port_init(&u8g2_port_cfg, u8g2);
    if (ESP_OK != err) {
        return err;
    }

    mui_t *mui = malloc(sizeof(mui_t));
    if (!mui) {
        err = ESP_ERR_NO_MEM;
        goto _cleanup;
    }
    mui_Init(mui, u8g2, fds_data, muif_list, sizeof(muif_list) / sizeof(muif_t));
    mui_GotoForm(mui, FORM_ID_MAIN, 0);

    u8g2_SetFont(u8g2, u8g2_font_ncenB08_tr);
    u8g2_ClearDisplay(u8g2);

    g_u8g2_handle = u8g2;
    g_mui_handle = mui;
    return ESP_OK;

_cleanup:
    if (u8g2) {
        free(u8g2);
        g_u8g2_handle = NULL;
    }
    if (g_mui_handle) {
        free(g_mui_handle);
        g_mui_handle = NULL;
    }
    return err;
}
