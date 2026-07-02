//
// Created by kafuumi on 2025/3/8.
//

#include <errno.h>
#include <string.h>

#include "diskio_impl.h"
#include "diskio_sdmmc.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"

#include "bsp_sd_card.h"

#define SD_CARD_SDMMC_SLOT SDMMC_HOST_SLOT_1
#define SD_CARD_SPI_SLOT SPI2_HOST

#define SD_CARD_CTX_SET_FLAG(ctx, flags, conn)                                                                         \
    {                                                                                                                  \
        if (conn) {                                                                                                    \
            (ctx)->flag |= flags;                                                                                      \
        } else {                                                                                                       \
            (ctx)->flag &= ~flags;                                                                                     \
        }                                                                                                              \
    }

#define SD_CARD_CTX_CHECK_FLAG(ctx, flags) ((ctx)->flag & flags)

extern const char *TAG;

#define SD_CARD_FLAG_SPI_BUS_INIT (1 << 0)
#define SD_CARD_FLAG_SDSPI_HOST_INIT (1 << 1)

typedef struct {
    sdspi_dev_handle_t card_dev;

    uint8_t flag;
    sdmmc_card_t *sdcard;
    FATFS *fs;
} sd_card_vfs_ctx_t;

static sd_card_vfs_ctx_t *sd_card_ctx = NULL;

static esp_err_t mount_prepare_mem(BYTE *out_pdrv, sdmmc_card_t **out_sd_card) {
    BYTE pdrv = FF_DRV_NOT_USED;
    if (ff_diskio_get_drive(&pdrv) != ESP_OK || pdrv == FF_DRV_NOT_USED) {
        ESP_LOGD(TAG, "the maximum count of volumes is already mounted");
        return ESP_ERR_NO_MEM;
    }
    sdmmc_card_t *card = malloc(sizeof(sdmmc_card_t));
    if (!card) {
        ESP_LOGD(TAG, "alloc sdmmc_card_t fail, no memory");
        return ESP_ERR_NO_MEM;
    }
    *out_pdrv = pdrv;
    *out_sd_card = card;
    return ESP_OK;
}

static esp_err_t mount_to_vfs_fat(const esp_vfs_fat_mount_config_t *mount_cfg, const char *mount_path, uint8_t pdrv,
                                  sdmmc_card_t *card, FATFS **out_fs) {
    FATFS *fs = NULL;
    esp_err_t err;
    ff_diskio_register_sdmmc(pdrv, card);
    ff_sdmmc_set_disk_status_check(pdrv, mount_cfg->disk_status_check_enable);
    ESP_LOGD(TAG, "ff_diskio register to pdrv=%i", pdrv);
    char drv[3] = {(char)('0' + pdrv), ':', 0};

    esp_vfs_fat_conf_t conf = {
        .base_path = mount_path,
        .fat_drive = drv,
        .max_files = mount_cfg->max_files,
    };
    err = esp_vfs_fat_register_cfg(&conf, &fs);
    if (ESP_ERR_INVALID_STATE == err) {
        // ignore, already registered
    } else if (ESP_OK != err) {
        ESP_LOGD(TAG, "register to vfs fat fail: %d", err);
        goto _cleanup;
    }
    // mount
    FRESULT ret = f_mount(fs, drv, 1);
    if (ret != FR_OK) {
        ESP_LOGD(TAG, "mount sd card fail: %d", ret);
        goto _cleanup;
    }
    return ESP_OK;

_cleanup:
    if (fs) {
        f_mount(NULL, drv, 0);
    }
    esp_vfs_fat_unregister_path(mount_path);
    ff_diskio_unregister(pdrv);
    return err;
}

static esp_err_t bsp_sd_card_sdspi_mount(const esp_vfs_fat_mount_config_t *mount_cfg, const char *mount_path) {
    esp_err_t err;

    sdmmc_host_t host_cfg = SDSPI_HOST_DEFAULT();
    host_cfg.max_freq_khz = SDMMC_FREQ_DEFAULT; // 20 MHz

    /* initialize spi bus */
    spi_bus_config_t bus_config = {
        .mosi_io_num = BSP_PIN_SD_CMD,
        .miso_io_num = BSP_PIN_SD_D0,
        .sclk_io_num = BSP_PIN_SD_CLK,
        .quadhd_io_num = GPIO_NUM_NC,
        .quadwp_io_num = GPIO_NUM_NC,
        .max_transfer_sz = 4096,
    };

    if (!SD_CARD_CTX_CHECK_FLAG(sd_card_ctx, SD_CARD_FLAG_SPI_BUS_INIT)) {
        err = spi_bus_initialize(SD_CARD_SPI_SLOT, &bus_config, SDSPI_DEFAULT_DMA);
        ESP_RETURN_ON_ERROR(err, TAG, "initialize spi bus fail: %d(%s)", err, esp_err_to_name(err));
    }
    SD_CARD_CTX_SET_FLAG(sd_card_ctx, SD_CARD_FLAG_SPI_BUS_INIT, true);

    /* prepare */
    sdmmc_card_t *sdcard;
    BYTE pdrv;
    err = mount_prepare_mem(&pdrv, &sdcard);
    ESP_RETURN_ON_ERROR(err, TAG, "no memory to mount sd card");

    /* initialize sdspi host */
    if (!SD_CARD_CTX_CHECK_FLAG(sd_card_ctx, SD_CARD_FLAG_SDSPI_HOST_INIT)) {
        err = sdspi_host_init();
        ESP_RETURN_ON_ERROR(err, TAG, "initialize sdspi host fail: %d(%s)", err, esp_err_to_name(err));
    }
    SD_CARD_CTX_SET_FLAG(sd_card_ctx, SD_CARD_FLAG_SDSPI_HOST_INIT, true);
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = BSP_PIN_SD_D3;
    slot_config.host_id = SD_CARD_SPI_SLOT;

    /* initialize sdspi device */
    sdspi_dev_handle_t dev_handle;
    err = sdspi_host_init_device(&slot_config, &dev_handle);
    ESP_RETURN_ON_ERROR(err, TAG, "initialize sdspi host device fail: %d(%s)", err, esp_err_to_name(err));
    bool flag_sdspi_dev_init = true;

    /* initialize sdmmc card */
    host_cfg.slot = dev_handle;
    err = sdmmc_card_init(&host_cfg, sdcard);
    esp_err_t ret; /* for ESP_GOTO_XXX micro */
    ESP_GOTO_ON_ERROR(err, _cleanup, TAG, "sdmmc card init fail: %d(%s)", err, esp_err_to_name(err));

    /* mount to vfs */
    FATFS *fs;
    err = mount_to_vfs_fat(mount_cfg, mount_path, pdrv, sdcard, &fs);
    ESP_GOTO_ON_ERROR(err, _cleanup, TAG, "mount sd card to vfs fail: %d(%s)", err, esp_err_to_name(err));

    sd_card_ctx->sdcard = sdcard;
    sd_card_ctx->fs = fs;
    return ESP_OK;

_cleanup:
    if (flag_sdspi_dev_init) {
        sdspi_host_remove_device(dev_handle);
    }
    free(sdcard);
    return err;
}

static esp_err_t bsp_sd_card_sdmmc_mount(const esp_vfs_fat_mount_config_t *mount_cfg, const char *mount_path) {
    sdmmc_host_t host_cfg = SDMMC_HOST_DEFAULT();
    host_cfg.max_freq_khz = SDMMC_FREQ_HIGHSPEED; // 40 MHz
    host_cfg.slot = SD_CARD_SDMMC_SLOT;

    sdmmc_slot_config_t slot_cfg = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_cfg.clk = BSP_PIN_SD_CLK;
    slot_cfg.cmd = BSP_PIN_SD_CMD;
    slot_cfg.d0 = BSP_PIN_SD_D0;
    slot_cfg.d1 = BSP_PIN_SD_D1;
    slot_cfg.d2 = BSP_PIN_SD_D2;
    slot_cfg.d3 = BSP_PIN_SD_D3;
    ESP_LOGI(TAG, "start mount sd card, clk:%d, cmd:%d, d0-d3: %d %d %d %d", slot_cfg.clk, slot_cfg.cmd, slot_cfg.d0,
             slot_cfg.d1, slot_cfg.d2, slot_cfg.d3);

    esp_err_t err;
    /* prepare */
    sdmmc_card_t *sdcard;
    BYTE pdrv;
    err = mount_prepare_mem(&pdrv, &sdcard);
    ESP_RETURN_ON_ERROR(err, TAG, "no memory to mount sd card");

    bool host_inited = false;
    esp_err_t ret;

    /* initialize sdmmc host  */
    err = sdmmc_host_init();
    ESP_GOTO_ON_ERROR(err, _cleanup, TAG, "sdmmc host init fail: %d(%s)", err, esp_err_to_name(err));
    host_inited = true;

    err = sdmmc_host_init_slot(host_cfg.slot, &slot_cfg);
    ESP_GOTO_ON_ERROR(err, _cleanup, TAG, "sdmmc host init slot fail: %d(%s)", err, esp_err_to_name(err));

    /* initialize sdmmc card */
    err = sdmmc_card_init(&host_cfg, sdcard);
    ESP_GOTO_ON_ERROR(err, _cleanup, TAG, "sdmmc card init fail: %d(%s)", err, esp_err_to_name(err));

    /* mount to vfs */
    FATFS *fs;
    err = mount_to_vfs_fat(mount_cfg, mount_path, pdrv, sdcard, &fs);
    ESP_GOTO_ON_ERROR(err, _cleanup, TAG, "mount sd card to vfs fail: %d(%s)", err, esp_err_to_name(err));

    sd_card_ctx->fs = fs;
    sd_card_ctx->sdcard = sdcard;
    return err;

_cleanup:
    if (host_inited) {
        sdmmc_host_deinit_slot(SD_CARD_SDMMC_SLOT);
    }
    free(sdcard);
    return err;
}

esp_err_t bsp_sd_card_mount() {
    if (sd_card_ctx && sd_card_ctx->sdcard) {
        ESP_LOGW(TAG, "sd card already mount");
        return ESP_OK;
    }
    esp_vfs_fat_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = true,
    };
    const char *mount_path = BSP_SD_CARD_MOUNT_POINT;
    sd_card_ctx = calloc(1, sizeof(sd_card_vfs_ctx_t));
    if (!sd_card_ctx) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err;
    err = bsp_sd_card_sdmmc_mount(&mount_cfg, mount_path);
    ESP_RETURN_ON_ERROR(err, TAG, "mount sd card to %s fail: %d(%s)", mount_path, err, esp_err_to_name(err));

    ESP_LOGI(TAG, "mount sd card on %s successful, card info:", mount_path);
    sdmmc_card_print_info(stdout, sd_card_ctx->sdcard);
    fflush(stdout);
    return ESP_OK;
}

esp_err_t sd_card_unmount() {
    // TODO
    ESP_RETURN_ON_FALSE(sd_card_ctx->sdcard, ESP_FAIL, TAG, "card is not mounted");
    const char *mount_path = BSP_SD_CARD_MOUNT_POINT;

    BYTE pdrv = ff_diskio_get_pdrv_card(sd_card_ctx->sdcard);
    char drv[3] = {(char)('0' + pdrv), ':', 0};
    f_mount(0, drv, 0);
    ff_diskio_unregister(pdrv);

    sdspi_host_remove_device(sd_card_ctx->card_dev);
    free(sd_card_ctx->sdcard);

    esp_vfs_fat_unregister_path(mount_path);

    sd_card_ctx->card_dev = 0;
    sd_card_ctx->sdcard = NULL;
    sd_card_ctx->fs = NULL;
    return ESP_OK;
}
