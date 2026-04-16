#include "drv8323.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "drv8323";

esp_err_t drv8323_init(DRV8323 *drv,
                        gpio_num_t cs, gpio_num_t en, gpio_num_t fault,
                        gpio_num_t sclk, gpio_num_t miso, gpio_num_t mosi) {
    drv->en_pin    = en;
    drv->fault_pin = fault;

    // EN als Output konfigurieren
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << en),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));

    // nFAULT als Input mit Pullup konfigurieren
    io.pin_bit_mask = (1ULL << fault);
    io.mode         = GPIO_MODE_INPUT;
    io.pull_up_en   = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK(gpio_config(&io));

    // SPI Bus initialisieren
    spi_bus_config_t buscfg = {
        .miso_io_num   = miso,
        .mosi_io_num   = mosi,
        .sclk_io_num   = sclk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    // DRV8323 als SPI Device hinzufuegen
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 1000000,
        .mode           = 1,
        .spics_io_num   = cs,
        .queue_size     = 4,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &devcfg, &drv->spi));

    // Driver aufwecken: EN low -> high
    gpio_set_level(en, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(en, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_LOGI(TAG, "DRV8323 initialized");
    return ESP_OK;
}

static uint16_t transfer_frame(DRV8323 *drv, uint16_t frame) {
    uint16_t tx = __builtin_bswap16(frame);
    spi_transaction_t t = {
        .length    = 16,
        .tx_buffer = &tx,
        .flags     = SPI_TRANS_USE_RXDATA,
    };
    spi_device_transmit(drv->spi, &t);
    return __builtin_bswap16(*(uint16_t*)t.rx_data);
}

uint16_t drv8323_read_reg(DRV8323 *drv, uint8_t reg) {
    uint16_t frame = (1 << 15) | ((reg & 0x0F) << 11);
    return transfer_frame(drv, frame) & 0x7FF;
}

esp_err_t drv8323_write_reg(DRV8323 *drv, uint8_t reg, uint16_t val) {
    uint16_t frame = ((reg & 0x0F) << 11) | (val & 0x7FF);
    transfer_frame(drv, frame);
    return ESP_OK;
}

bool drv8323_has_fault(DRV8323 *drv) {
    return gpio_get_level(drv->fault_pin) == 0;
}

uint16_t drv8323_read_fault1(DRV8323 *drv) { return drv8323_read_reg(drv, 0x00); }
uint16_t drv8323_read_fault2(DRV8323 *drv) { return drv8323_read_reg(drv, 0x01); }

esp_err_t drv8323_clear_faults(DRV8323 *drv) {
    gpio_set_level(drv->en_pin, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(drv->en_pin, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    return ESP_OK;
}
