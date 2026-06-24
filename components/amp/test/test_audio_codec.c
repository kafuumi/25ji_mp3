
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "amp/audio_codec.h"
#include "amp/ringbuf.h"
#include "esp_log.h"
#include "unity.h"

static const char *TAG = "test_amp_audio_codec";

static void read_file_data_task(void *args) {
    const char *filename = "/storage/music/test.mp3";
    RingbufHandle_t rb = args;
    TEST_ASSERT_NOT_NULL(rb);

    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        ESP_LOGE(TAG, "open file %s fail: %s", filename, strerror(errno));
        TEST_ASSERT_NOT_NULL(fp);
        return;
    }
    size_t buf_size = 512;
    uint8_t *buf = malloc(sizeof(uint8_t) * buf_size);
    while (true) {
        size_t rc = fread(buf, sizeof(uint8_t), buf_size, fp);
        if (rc < 0) {
            ESP_LOGE(TAG, "fread %s fail: %s", filename, strerror(errno));
            break;
        } else if (rc == 0) {
            ESP_LOGI(TAG, "fread %s finished", filename);
            break;
        }
        // ESP_LOGI(TAG, "fread %s size: %d", filename, rc);
        xRingbufferSend(rb, buf, rc, portMAX_DELAY);
    }
    if (fp)
        fclose(fp);
    vTaskDelete(NULL);
}

TEST_CASE("Audio Codec run codec task", "[amp][audio_codec]") {
    audio_codec_handle_t *codec;
    esp_err_t err = audio_codec_init(&codec);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_NOT_NULL(codec);

    const amp_element_interface_t *intf = audio_codec_el_interface();
    TEST_ASSERT_NOT_NULL(intf);
    TEST_ASSERT_NOT_NULL(intf->task_run);
    TEST_ASSERT_TRUE(intf->set_input_rb && intf->set_output_rb);

    ringbuf_handle_t rb_in = rb_create(sizeof(uint8_t), 4096);
    ringbuf_handle_t rb_out = rb_create(sizeof(uint8_t), 4096);

    intf->set_input_rb(codec, rb_in);
    intf->set_output_rb(codec, rb_out);

    TaskHandle_t codec_task;
    xTaskCreate(intf->task_run, "codec", 4096, codec, 1, &codec_task);

    TaskHandle_t read_task;
    xTaskCreate(read_file_data_task, "read", 4096, rb_in, 1, &read_task);

    while (true) {
        size_t item_size = 0;
        void *item = xRingbufferReceive(rb_out, &item_size, portMAX_DELAY);
        ESP_LOGI(TAG, "receive item size: %d", item_size);
        vRingbufferReturnItem(rb_out, item);
    }
}
