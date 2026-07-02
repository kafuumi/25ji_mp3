#include "esp_err.h"
#include "unity.h"

#include "bsp_sd_card.h"

static const char *TAG = "test_bsp";

void bench_test() { unity_run_tests_by_tag("[bench]", false); }

void app_main() {
    esp_err_t err = bsp_sd_card_mount();
    TEST_ASSERT_EQUAL(ESP_OK, err);

    // unity_run_all_tests();
    bench_test();
}
