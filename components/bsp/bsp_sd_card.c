//
// Created by kafuumi on 2025/3/8.
//

#include <string.h>
#include <errno.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#include "bsp_sd_card.h"
#include "bsp.h"

#define SDMMC_SLOT SDMMC_HOST_SLOT_1
#define SD_CARD_SPI_SLOT SPI2_HOST

static const char *TAG = "bsp";
static sdmmc_card_t *card = NULL;

esp_err_t bsp_sd_card_init() {
    esp_vfs_fat_mount_config_t mount_cfg = {
        .format_if_mount_failed = true,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = true,
    };
    const char mount_path[] = BSP_SD_CARD_MOUNT_POINT;
    esp_err_t err = ESP_OK;
#ifdef CONFIG_SD_CARD_SDIO_MODE
    // sdio
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_SLOT;
    host.flags = SDMMC_HOST_FLAG_4BIT;
    host.flags |= SDMMC_HOST_FLAG_ALLOC_ALIGNED_BUF;
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

    sdmmc_slot_config_t slot_cfg = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_cfg.clk = BOARD_PIN_SD_CLK;
    slot_cfg.cmd = BOARD_PIN_SD_CMD;
    slot_cfg.d0 = BOARD_PIN_SD_D0;
    slot_cfg.d1 = BOARD_PIN_SD_D1;
    slot_cfg.d2 = BOARD_PIN_SD_D2;
    slot_cfg.d3 = BOARD_PIN_SD_D3;
    ESP_LOGI(TAG, "start initialize sd card, clk:%d, cmd:%d, d0-d3: %d %d %d %d",
        slot_cfg.clk, slot_cfg.cmd, slot_cfg.d0, slot_cfg.d1, slot_cfg.d2, slot_cfg.d3);
    esp_err_t err = esp_vfs_fat_sdmmc_mount(mount_path, &host, &slot_cfg, &mount_cfg, &card);
#elif CONFIG_SD_CARD_SPI_MODE
    // spi
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
    host.slot = SD_CARD_SPI_SLOT;
    spi_bus_config_t bus_config = {
        .mosi_io_num = BSP_PIN_SD_CMD,
        .miso_io_num = BSP_PIN_SD_D0,
        .sclk_io_num = BSP_PIN_SD_CLK,
        .quadhd_io_num = GPIO_NUM_NC,
        .quadwp_io_num = GPIO_NUM_NC,
        .max_transfer_sz = 4096,
    };

    err = spi_bus_initialize(host.slot, &bus_config, SDSPI_DEFAULT_DMA);
    ESP_RETURN_ON_ERROR(err, TAG, "initialize spi bus fail: %d(%s)", err, esp_err_to_name(err));
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = BSP_PIN_SD_D3;
    slot_config.host_id = host.slot;
    esp_vfs_fat_sdspi_mount(mount_path, &host, &slot_config, &mount_cfg, &card);
#endif
    if (ESP_OK != err) {
        if (ESP_FAIL == err) {
            ESP_LOGE(TAG, "failed to mount filesystem, need format sd card first");
        } else if (ESP_ERR_TIMEOUT == err) {
            ESP_LOGE(TAG, "initialize sd card timeout, check the connection, err: %d(%s)", err, esp_err_to_name(err));
        } else {
            ESP_LOGE(TAG, "initialize sd card fail: %d(%s)", err, esp_err_to_name(err));
        }
        return err;
    }
    ESP_LOGI(TAG, "mount sd card on %s successful, card info:", mount_path);
    sdmmc_card_print_info(stdout, card);
    fflush(stdout);
    return ESP_OK;
}

esp_err_t bsp_sd_card_format() {
    ESP_RETURN_ON_FALSE(card, ESP_FAIL, TAG, "card is not mounted");
    const char mount_path[] = BSP_SD_CARD_MOUNT_POINT;

    esp_err_t err = esp_vfs_fat_sdcard_format(mount_path, card);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "format sd card fail: %d(%s)", err, esp_err_to_name(err));
    }
    return err;
}

esp_err_t sd_card_unmount() {
    ESP_RETURN_ON_FALSE(card, ESP_FAIL, TAG, "card is not mounted");
    const char mount_path[] = BSP_SD_CARD_MOUNT_POINT;
    esp_err_t err = esp_vfs_fat_sdcard_unmount(mount_path, card);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "unmount sd card fail: %d(%s)", err, esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "unmount sd card successful");
    return err;
}

bool bsp_sd_card_rw_test() {
    ESP_LOGI(TAG, "start test sd card by read and write");
    ESP_RETURN_ON_FALSE(card, ESP_FAIL, TAG, "card is not mounted");
    const char test_path[] = BSP_SD_CARD_MOUNT_POINT "/test.bin";
    const char test_data[] = "hello sd_card";

    FILE *fp = fopen(test_path, "w");
    bool ret = false;
    if (NULL == fp) {
        ESP_LOGE(TAG, "failed to open test file, err:%d(%s)", errno, strerror(errno));
        return false;
    }
    int wn = fprintf(fp, test_data);
    if (wn < strlen(test_data)) {
        ESP_LOGE(TAG, "failed to write file to sd card, err:%d(%s)", errno, strerror(errno));
        ret = false;
        goto cleanup;
    }
    fflush(fp);
    fclose(fp);
    fp = NULL;

    // test read
    ESP_LOGI(TAG, "write file to sd card successful, try read file");
    fp = fopen(test_path, "r");
    if (NULL == fp) {
        ESP_LOGE(TAG, "failed to open test file, err:%d(%s)", errno, strerror(errno));
        ret = false;
        return false;
    }
    char buf[10] = {'\0'};
    rewind(fp);
    if (!fgets(buf, sizeof(buf), fp)) {
        ESP_LOGE(TAG, "failed to read test file, err:%d(%s)", errno, strerror(errno));
        ret = false;
        goto cleanup;
    }
    ESP_LOGI(TAG, "read file from sd card success, content:%s", buf);
    ret = true;
cleanup:
    fclose(fp);
    remove(test_path);
    return ret;
}