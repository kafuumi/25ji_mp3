#include "bsp_sd_card.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "unity.h"
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

static const char *TAG = "bsp_sd_card_test";

const char test_path[] = BSP_SD_CARD_MOUNT_POINT "/test.bin";
const char test_data[] = "hello sd_card";

static void bsp_sd_card_test_write() {
    int fd = open(test_path, O_WRONLY | O_CREAT, 0644);
    if (fd < 0) {
        ESP_LOGE(TAG, "open test file %s fail: %s", test_path, strerror(errno));
    }

    TEST_ASSERT_GREATER_OR_EQUAL(0, fd);

    const char *buf = test_data;
    size_t buf_len = strlen(test_data);
    size_t wn = write(fd, buf, buf_len);
    if (wn < buf_len) {
        ESP_LOGE(TAG, "failed to write file to sd card, err: (%s)", strerror(errno));
    }
    ESP_LOGI(TAG, "test sd card write, write size: %d", wn);
    TEST_ASSERT_EQUAL(buf_len, wn);

    close(fd);
}

TEST_CASE("test sd card write", "[bsp][sd]") { bsp_sd_card_test_write(); }

TEST_CASE("test sd card read and write", "[bsp][sd]") {

    bsp_sd_card_test_write();
    int fd = open(test_path, O_RDONLY);
    if (fd < 0) {
        ESP_LOGE(TAG, "open test file %s fail: %s", test_path, strerror(errno));
    }
    TEST_ASSERT_GREATER_OR_EQUAL(0, fd);

    char buf[128] = {0};
    size_t rn = read(fd, buf, sizeof(buf));
    if (rn < strlen(test_data)) {
        ESP_LOGE(TAG, "failed to write file to sd card, err:%d(%s)", errno, strerror(errno));
    }
    buf[rn] = '\0';
    ESP_LOGI(TAG, "test sd card read, write size: %d, raw: %s", rn, buf);
    TEST_ASSERT_EQUAL(strlen(test_data), rn);

    close(fd);
}

TEST_CASE("bench test sd card rw", "[bsp][sd][bench]") {
    size_t buf_len = 1024;
    uint8_t *buf = malloc(sizeof(uint8_t) * buf_len);
    TEST_ASSERT_NOT_NULL(buf);
    for (int i = 0; i < buf_len; ++i) {
        buf[i] = i;
    }
    int loop_times = 1024;
    int fd = open(test_path, O_RDWR | O_CREAT, 0644);
    if (fd < 0) {
        ESP_LOGE(TAG, "open test file %s fail: %s", test_path, strerror(errno));
    }
    TEST_ASSERT_GREATER_OR_EQUAL(0, fd);

    // write test
    size_t wn = 0;
    int64_t start = esp_timer_get_time();
    for (int i = 0; i < loop_times; ++i) {
        wn = wn + write(fd, buf, buf_len);
    }
    int64_t end = esp_timer_get_time();
    float write_rate = (float)wn * 1000.0f / (float)(end - start);
    ESP_LOGI(TAG, "bench test write, size: %d B", wn);

    lseek(fd, 0, SEEK_SET);
    // read test
    size_t rn = 0;
    start = esp_timer_get_time();
    for (int i = 0; i < loop_times; ++i) {
        rn += read(fd, buf, buf_len);
    }
    end = esp_timer_get_time();
    float read_rate = (float)rn * 1000.0f / (float)(end - start);
    ESP_LOGI(TAG, "bench test read, size: %d B", rn);

    ESP_LOGI(TAG, "bench test rw, write: %.2f B/ms, read: %.2f B/ms", write_rate, read_rate);
    free(buf);
    close(fd);
}
