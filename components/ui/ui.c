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

#define UI_FPS 10
#define INPUT_QUEUE_SIZE 8
#define DRAW_TASK_NOTIFY_MASK_REDRAW (1 << 0)

/////////////////////////////////////////////

static const char *TAG = "ui";

typedef struct {
    int padding;
    int margin;

    ui_main_state_t state;
    const char *media_type;
    int vol;
    const char *title;
    int sample_rate;
    int channel;
    int bit_width;
} MainUIContext_t;

typedef struct {
    u8g2_t u8g2;
    mui_t mui;
    TaskHandle_t mui_task;
    QueueHandle_t input_queue;

    int screen_w, screen_h;
    bool redraw;

    MainUIContext_t ui_main;
} UIContext_t;

static UIContext_t *g_ui_ctx = NULL;

#define UI_RENDER(_u8g2, _draw_func)                                                                                   \
    do {                                                                                                               \
        u8g2_FirstPage(_u8g2);                                                                                         \
        do {                                                                                                           \
            _draw_func                                                                                                 \
        } while (u8g2_NextPage(_u8g2));                                                                                \
    } while (0)

#define MUI_FLUSH(_mui, _u8g2) UI_RENDER(_u8g2, mui_Draw(_mui);)

#define UI_FONT_SIZE(u8g2) (u8g2_GetFontAscent((u8g2)) - u8g2_GetFontDescent((u8g2)))

static inline void ui_main_form_draw() {
    MainUIContext_t *ui_main = &g_ui_ctx->ui_main;
    u8g2_t *u8g2 = &g_ui_ctx->u8g2;
    ui_main->padding = 8;
    ui_main->margin = 4;

    UI_RENDER(&g_ui_ctx->u8g2, {
        int pos_x = ui_main->margin;
        int pos_y = ui_main->margin;
        int font_size = 0;
#define STR_BUT_SIZE 32
        char str_buf[STR_BUT_SIZE] = {0};
        // header
        {
            switch (ui_main->state) {
            case UI_MAIN_STATE_INVALID:
                strcat(str_buf, "[----] ");
                break;
            case UI_MAIN_STATE_PLAY:
                strcat(str_buf, "[PLAY] ");
                break;
            case UI_MAIN_STATE_PAUSE:
                strcat(str_buf, "[PAUSE] ");
                break;
            case UI_MAIN_STATE_FAIL:
                strcat(str_buf, "[FAIL] ");
                break;
            }
            if (ui_main->media_type == NULL || strlen(ui_main->media_type) == 0) {
                strcat(str_buf, "--");
            } else {
                strcat(str_buf, ui_main->media_type);
            }
            u8g2_SetFont(u8g2, u8g2_font_6x12_mf);
            font_size = UI_FONT_SIZE(u8g2);
            int draw_y = pos_y + u8g2_GetAscent(u8g2);
            u8g2_DrawStr(u8g2, pos_x, draw_y, str_buf);
            str_buf[0] = '\0';
            snprintf(str_buf, STR_BUT_SIZE, "VOL %02d", ui_main->vol);
            u8g2_DrawStr(u8g2, g_ui_ctx->screen_w - ui_main->margin - u8g2_GetStrWidth(u8g2, str_buf), draw_y, str_buf);
            pos_y += font_size;
            u8g2_DrawHLine(u8g2, 0, pos_y, g_ui_ctx->screen_w);
        }
        // title
        {
            str_buf[0] = '\0';
            pos_y += ui_main->padding * 2;
            u8g2_SetFont(u8g2, u8g2_font_wqy12_t_gb2312);
            font_size = UI_FONT_SIZE(u8g2);
            int draw_y = pos_y + u8g2_GetAscent(u8g2);
            if (ui_main->title == NULL || strlen(ui_main->title) == 0) {
                u8g2_DrawStr(u8g2, pos_x, draw_y, "--");
            } else {
                u8g2_DrawUTF8(u8g2, pos_x, draw_y, ui_main->title);
            }
        }
        // params
        {
            str_buf[0] = '\0';
            u8g2_SetFont(u8g2, u8g2_font_6x12_mf);
            font_size = UI_FONT_SIZE(u8g2);
            pos_y = g_ui_ctx->screen_h - ui_main->margin - font_size * 2;
            u8g2_DrawHLine(u8g2, 0, pos_y, g_ui_ctx->screen_w);
            int draw_y = pos_y + font_size + u8g2_GetAscent(u8g2);
            if (ui_main->sample_rate == 0) {
                u8g2_DrawStr(u8g2, pos_x, draw_y, "- kHz");
            } else {
                float rate = (float)ui_main->sample_rate / 1000.0f;
                snprintf(str_buf, STR_BUT_SIZE, "%.1f kHz", rate);
                u8g2_DrawStr(u8g2, pos_x, draw_y, str_buf);
            }
            str_buf[0] = '\0';
            snprintf(str_buf, STR_BUT_SIZE, "%dch %dbit", ui_main->channel, ui_main->bit_width);
            u8g2_DrawStr(u8g2, g_ui_ctx->screen_w - ui_main->margin - u8g2_GetStrWidth(u8g2, str_buf), draw_y, str_buf);
        }
    });
}
#undef STR_BUT_SIZE

static void mui_draw_task(void *args) {

    TickType_t wait = pdMS_TO_TICKS(1000 / 30);
    g_ui_ctx->redraw = true;

    uint32_t notify = 0;
    while (true) {
        if (xTaskNotifyWait(0, ULONG_MAX, &notify, wait) == pdTRUE) {
            if (notify & DRAW_TASK_NOTIFY_MASK_REDRAW) {
                g_ui_ctx->redraw = true;
            }
        }
        if (g_ui_ctx->redraw) {
            ui_main_form_draw();
            g_ui_ctx->redraw = false;
        }
        // vTaskDelay(wait);
    }
}

void ui_main_set_info(ui_main_info_t *info) {
    MainUIContext_t *ui_main = &g_ui_ctx->ui_main;
    if (info->state != UI_MAIN_STATE_INVALID) {
        ui_main->state = info->state;
    }
    if (info->media_type) {
        ui_main->media_type = info->media_type;
    }
    if (info->vol > 0) {
        ui_main->vol = info->vol - 1;
    }
    if (info->title) {
        ui_main->title = info->title;
    }
    if (info->sample_rate) {
        ui_main->sample_rate = info->sample_rate;
    }
    if (info->channel) {
        ui_main->channel = info->channel;
    }
    if (info->bit_width) {
        ui_main->bit_width = info->bit_width;
    }
    if (g_ui_ctx->mui_task) {

        xTaskNotify(g_ui_ctx->mui_task, DRAW_TASK_NOTIFY_MASK_REDRAW, eSetBits);
    }
}

void ui_main_set_volume_info(int volume) {
    ui_main_info_t info = {
        .vol = volume + 1,
    };
    ui_main_set_info(&info);
}

esp_err_t ui_post_input(ui_input_event_t event) {
    // BaseType_t ret = xQueueSend(g_ui_ctx->input_queue, &event, 0);
    // if (ret != pdTRUE) {
    //     return ESP_ERR_TIMEOUT;
    // }
    return ESP_OK;
}

esp_err_t ui_start(ui_cfg_t *cfg) {
    if (!g_ui_ctx) {
        return ESP_ERR_INVALID_STATE;
    }
    QueueHandle_t input_queue = xQueueCreate(INPUT_QUEUE_SIZE, sizeof(ui_input_event_t));
    if (!input_queue) {
        return ESP_ERR_NO_MEM;
    }
    g_ui_ctx->input_queue = input_queue;

    TaskHandle_t task;
    BaseType_t ret = xTaskCreatePinnedToCore(mui_draw_task, cfg->task_name, cfg->task_size, NULL, cfg->task_priority,
                                             &task, cfg->task_cpu_num);
    if (ret != pdTRUE) {
        vQueueDelete(input_queue);
        g_ui_ctx->input_queue = NULL;
        return ESP_FAIL;
    }
    g_ui_ctx->mui_task = task;
    return ESP_OK;
}

esp_err_t ui_init(i2c_master_bus_handle_t i2c_bus_handle) {
    if (g_ui_ctx) {
        return ESP_ERR_INVALID_STATE;
    }
    UIContext_t *ui_ctx = calloc(1, sizeof(UIContext_t));
    if (!ui_ctx) {
        return ESP_ERR_NO_MEM;
    }
    const u8g2_port_i2c_config_t u8g2_port_cfg = {
        .i2c_bus = i2c_bus_handle,
        .rotation = ROTATION_180,
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .dev_address = 0x3C,
        .scl_freq_hz = 100 * 1000,
        .buf_size = 32,
    };

    esp_err_t err = u8g2_port_init(&u8g2_port_cfg, &ui_ctx->u8g2);
    if (ESP_OK != err) {
        goto _cleanup;
    }

    // mui_Init(mui, u8g2, fds_data, muif_list, sizeof(muif_list) / sizeof(muif_t));
    // mui_GotoForm(mui, FORM_ID_MAIN, 0);

    u8g2_ClearDisplay(&ui_ctx->u8g2);
    ui_ctx->screen_w = u8g2_GetDisplayWidth(&ui_ctx->u8g2);
    ui_ctx->screen_h = u8g2_GetDisplayHeight(&ui_ctx->u8g2);
    ESP_LOGI(TAG, "screen size %d x %d", ui_ctx->screen_w, ui_ctx->screen_h);
    ui_ctx->ui_main.state = UI_MAIN_STATE_PAUSE;
    g_ui_ctx = ui_ctx;
    return ESP_OK;

_cleanup:
    if (ui_ctx) {
        free(ui_ctx);
    }
    g_ui_ctx = NULL;
    return err;
}
