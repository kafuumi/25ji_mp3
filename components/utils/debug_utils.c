#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#include "utils/debug_utils.h"

static const char *TAG = "debug";

struct mem_pool_t {
    const char *name;
    uint32_t caps;
};

void debug_print_heap_info(void) {
    ESP_LOGI(TAG, "========== HEAP INFO ==========");

    static const struct mem_pool_t pools[] = {
        {"DRAM", MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL},
        {"DMA", MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL},
#ifdef CONFIG_SPIRAM
        {"PSRAM", MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM},
#endif
    };

    for (size_t i = 0; i < sizeof(pools) / sizeof(pools[0]); i++) {
        ESP_LOGI(TAG, "%-5s  free: %-7u min_free: %-7u largest: %-7u", pools[i].name,
                 heap_caps_get_free_size(pools[i].caps), heap_caps_get_minimum_free_size(pools[i].caps),
                 heap_caps_get_largest_free_block(pools[i].caps));
    }
    multi_heap_info_t info = {0};
    heap_caps_get_info(&info, MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "alloc: %u", info.total_allocated_bytes);
    ESP_LOGI(TAG, "==============================");
}

static void print_heap_info_task(void *args) {
    while (true) {
        debug_print_heap_info();
        vTaskDelay(pdMS_TO_TICKS(10 * 1000));
    }
}

void start_print_heap_task() { xTaskCreate(print_heap_info_task, "heap_info", 4096, NULL, 5, NULL); }
