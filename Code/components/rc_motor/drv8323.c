#include "drv8323.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/task.h"

static const char *TAG = "drv8323";

static const uint16_t REG_DRV_CTRL    = (1 << 8);
static const uint16_t REG_GATE_DRV_HS = (3 << 8) | (11 << 4) | 15;
static const uint16_t REG_GATE_DRV_LS = (1 << 10) | (2 << 8) | (15 << 4) | 15;
static const uint16_t REG_OCP_CTRL    = (1 << 8) | (1 << 6) | (2 << 4);
static const uint16_t REG_CSA_CTRL    = (1 << 10) | (1 << 9) | (2 << 6) | 3;

static const spi_bus_config_t BUS_CFG_DEFAULTS = {
    .miso_io_num     = -1,
    .mosi_io_num     = -1,
    .sclk_io_num     = -1,
    .quadwp_io_num   = -1,
    .quadhd_io_num   = -1,
    .max_transfer_sz = 2,
};

static void write_all_regs(DRV8323 *drv)
{
    drv8323_write_reg(drv, ADR_DRV_CTRL,    REG_DRV_CTRL);    vTaskDelay(pdMS_TO_TICKS(1));
    drv8323_write_reg(drv, ADR_GATE_DRV_HS, REG_GATE_DRV_HS); vTaskDelay(pdMS_TO_TICKS(1));
    drv8323_write_reg(drv, ADR_GATE_DRV_LS, REG_GATE_DRV_LS); vTaskDelay(pdMS_TO_TICKS(1));
    drv8323_write_reg(drv, ADR_OCP_CTRL,    REG_OCP_CTRL);    vTaskDelay(pdMS_TO_TICKS(1));
    drv8323_write_reg(drv, ADR_CSA_CTRL,    REG_CSA_CTRL);    vTaskDelay(pdMS_TO_TICKS(1));
}

esp_err_t drv8323_init(DRV8323 *drv,
                        gpio_num_t cs, gpio_num_t en, gpio_num_t fault,
                        gpio_num_t sclk, gpio_num_t miso, gpio_num_t mosi)
{
    drv->en_pin    = en;
    drv->fault_pin = fault;

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << en),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_level(en, 1);
    vTaskDelay(pdMS_TO_TICKS(1));

    io.pin_bit_mask = (1ULL << fault);
    io.mode         = GPIO_MODE_INPUT;
    io.pull_up_en   = GPIO_PULLUP_ENABLE;
    gpio_config(&io);

    spi_bus_config_t bus = BUS_CFG_DEFAULTS;
    bus.miso_io_num = miso;
    bus.mosi_io_num = mosi;
    bus.sclk_io_num = sclk;

    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_DISABLED);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %d", ret);
        return ret;
    }

    spi_device_interface_config_t dev_cfg = {
        .command_bits     = 0,
        .address_bits     = 0,
        .dummy_bits       = 0,
        .mode             = 1,
        .clock_speed_hz   = 1000000,
        .spics_io_num     = cs,
        .queue_size       = 1,
        .cs_ena_pretrans  = 2,
        .cs_ena_posttrans = 2,
    };
    ret = spi_bus_add_device(SPI2_HOST, &dev_cfg, &drv->spi);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device failed: %d", ret);
        return ret;
    }

    // DRV8323 SDO is open-drain — needs pull-up, ESP-IDF does not set this automatically
    gpio_pullup_en(miso);
    vTaskDelay(pdMS_TO_TICKS(5));

    write_all_regs(drv);

    ESP_LOGI(TAG, "DRV8323 regs: FAULT=%03X VGS=%03X CTRL=%03X HS=%03X LS=%03X OCP=%03X CSA=%03X",
             drv8323_read_reg(drv, ADR_FAULT_STAT),
             drv8323_read_reg(drv, ADR_VGS_STAT),
             drv8323_read_reg(drv, ADR_DRV_CTRL),
             drv8323_read_reg(drv, ADR_GATE_DRV_HS),
             drv8323_read_reg(drv, ADR_GATE_DRV_LS),
             drv8323_read_reg(drv, ADR_OCP_CTRL),
             drv8323_read_reg(drv, ADR_CSA_CTRL));

    return ESP_OK;
}

uint16_t drv8323_read_reg(DRV8323 *drv, uint8_t reg)
{
    uint8_t tx[2] = { (uint8_t)(0x80 | (reg << 3)), 0x00 };
    uint8_t rx[2] = { 0 };
    spi_transaction_t t = {
        .length    = 16,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    spi_device_transmit(drv->spi, &t);
    return (uint16_t)(((rx[0] & 0x07) << 8) | rx[1]);
}

esp_err_t drv8323_write_reg(DRV8323 *drv, uint8_t reg, uint16_t val)
{
    uint8_t tx[2] = {
        (uint8_t)((reg << 3) | ((val >> 8) & 0x07)),
        (uint8_t)(val & 0xFF),
    };
    spi_transaction_t t = {
        .length    = 16,
        .tx_buffer = tx,
        .rx_buffer = NULL,
    };
    return spi_device_transmit(drv->spi, &t);
}

bool drv8323_has_fault(DRV8323 *drv)
{
    return gpio_get_level(drv->fault_pin) == 0;
}

uint16_t drv8323_read_fault1(DRV8323 *drv)
{
    return drv8323_read_reg(drv, ADR_FAULT_STAT);
}

uint16_t drv8323_read_fault2(DRV8323 *drv)
{
    return drv8323_read_reg(drv, ADR_VGS_STAT);
}

esp_err_t drv8323_clear_faults(DRV8323 *drv)
{
    drv8323_write_reg(drv, ADR_DRV_CTRL, REG_DRV_CTRL | 0x0001);
    vTaskDelay(pdMS_TO_TICKS(1));
    write_all_regs(drv);
    return ESP_OK;
}
