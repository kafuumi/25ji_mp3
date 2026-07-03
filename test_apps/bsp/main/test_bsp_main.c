#include "esp_err.h"
#include "unity.h"

#include "bsp_sd_card.h"

static const char *TAG = "test_bsp";

void bench_test() { unity_run_tests_by_tag("[bench]", false); }

void app_main() {

    unity_run_all_tests();
    // bench_test();
}
