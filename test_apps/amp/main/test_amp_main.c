#include "esp_err.h"
#include "esp_vfs_fat.h"
#include "unity.h"

static const char *TAG = "test_amp";

static wl_handle_t wl_handle = WL_INVALID_HANDLE;

static esp_err_t fatfs_vfs_register() {
    esp_vfs_fat_mount_config_t mount_cfg = {
        .max_files = 4,
        .format_if_mount_failed = true,
        .allocation_unit_size = 4096,
        .use_one_fat = false,
    };
    esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl("/storage", "storage", &mount_cfg, &wl_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mount fast spiflash fail: %s", esp_err_to_name(err));
    }
    return err;
}

void app_main() {
    esp_err_t err = fatfs_vfs_register();
    TEST_ASSERT_EQUAL(err, ESP_OK);
    ESP_LOGI(TAG, "moun storage success, path: /storage");
    unity_run_menu();
    // unity_run_test_by_index(2);
    esp_vfs_fat_spiflash_unmount_rw_wl("/storage", wl_handle);
    ESP_LOGI(TAG, "test finished");
}
