//
// Created by kafuumi on 2025/3/8.
//

#include <errno.h>
#include <string.h>

#include "diskio_sdmmc.h"
#include "driver/sdspi_host.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"

#include "bsp_sd_card.h"

#define SDMMC_SLOT SDMMC_HOST_SLOT_1
#define SD_CARD_SPI_SLOT SPI2_HOST

static const char *TAG = "bsp_sd";

static bool flag_spi_bus_init = false;
static bool flag_sdspi_host_init = false;
static sdmmc_card_t *card = NULL;

static esp_err_t mount_to_vfs_fat(esp_vfs_fat_mount_config_t mount_cfg, const char *mount_path, uint8_t pdrv,
                                  sdmmc_card_t *card) {
    FATFS *fs = NULL;
    esp_err_t err;
    ff_diskio_register_sdmmc(pdrv, card);
    ff_sdmmc_set_disk_status_check(pdrv, mount_cfg.disk_status_check_enable);
    char drv[3] = {(char)('0' + pdrv), ':', 0};

    esp_vfs_fat_conf_t conf = {
        .base_path = mount_path,
        .fat_drive = drv,
        .max_files = mount_cfg.max_files,
    };
    err = esp_vfs_fat_register_cfg(&conf, &fs);
}

static esp_err_t bsp_sd_card_sdspi_init(const char *mount_path) {
    esp_err_t err;

    sdmmc_host_t host_cfg = SDSPI_HOST_DEFAULT();
    host_cfg.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

    /* initialize spi bus */
    spi_bus_config_t bus_config = {
        .mosi_io_num = BSP_PIN_SD_CMD,
        .miso_io_num = BSP_PIN_SD_D0,
        .sclk_io_num = BSP_PIN_SD_CLK,
        .quadhd_io_num = GPIO_NUM_NC,
        .quadwp_io_num = GPIO_NUM_NC,
        .max_transfer_sz = 4096,
    };
    if (!flag_spi_bus_init) {
        err = spi_bus_initialize(SD_CARD_SPI_SLOT, &bus_config, SDSPI_DEFAULT_DMA);
        ESP_RETURN_ON_ERROR(err, TAG, "initialize spi bus fail: %d(%s)", err, esp_err_to_name(err));
    }
    flag_spi_bus_init = true;

    /* initialize sdspi host */
    if (!flag_sdspi_host_init) {
        err = sdspi_host_init();
        ESP_RETURN_ON_ERROR(err, TAG, "initialize sdspi host fail: %d(%s)", err, esp_err_to_name(err));
    }
    flag_sdspi_host_init = true;
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = BSP_PIN_SD_D3;
    slot_config.host_id = SD_CARD_SPI_SLOT;
    // esp_vfs_fat_sdspi_mount(mount_path, &host_cfg, &slot_config, &mount_cfg, &card);
    sdspi_dev_handle_t card_handle;
    err = sdspi_host_init_device(&slot_config, &card_handle);
    ESP_RETURN_ON_ERROR(err, TAG, "initialize sdspi host device fail: %d(%s)", err, esp_err_to_name(err));
    bool flag_sdspi_dev_init = true;

    /* initialize sdmmc card */
    esp_err_t ret; /* for ESP_GOTO_XXX micro */
    host_cfg.slot = card_handle;
    sdmmc_card_t *sdcard = malloc(sizeof(sdmmc_card_t));
    if (!sdcard) {
        err = ESP_ERR_NO_MEM;
        goto _cleanup;
    }
    err = sdmmc_card_init(&host_cfg, sdcard);
    ESP_GOTO_ON_ERROR(err, _cleanup, TAG, "sdmmc card init fail: %d(%s)", err, esp_err_to_name(err));

    /* mount sdcard to vfs */

    return err;

_cleanup:
    if (flag_sdspi_dev_init) {
        sdspi_host_remove_device(card_handle);
    }
    if (sdcard) {
        free(sdcard);
    }
}

esp_err_t bsp_sd_card_init() {
    esp_vfs_fat_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = true,
    };

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
    ESP_LOGI(TAG, "start initialize sd card, clk:%d, cmd:%d, d0-d3: %d %d %d %d", slot_cfg.clk, slot_cfg.cmd,
             slot_cfg.d0, slot_cfg.d1, slot_cfg.d2, slot_cfg.d3);
    esp_err_t err = esp_vfs_fat_sdmmc_mount(mount_path, &host, &slot_cfg, &mount_cfg, &card);
#elif CONFIG_SD_CARD_SPI_MODE
    // spi

#endif

_init_finished:
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