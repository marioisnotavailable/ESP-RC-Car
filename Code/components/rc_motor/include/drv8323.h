#pragma once
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_err.h"

typedef struct {
    spi_device_handle_t spi;
    gpio_num_t          en_pin;
    gpio_num_t          fault_pin;
} DRV8323;

esp_err_t drv8323_init(DRV8323 *drv,
                        gpio_num_t cs, gpio_num_t en, gpio_num_t fault,
                        gpio_num_t sclk, gpio_num_t miso, gpio_num_t mosi);
uint16_t  drv8323_read_reg(DRV8323 *drv, uint8_t reg);
esp_err_t drv8323_write_reg(DRV8323 *drv, uint8_t reg, uint16_t val);
bool      drv8323_has_fault(DRV8323 *drv);
uint16_t  drv8323_read_fault1(DRV8323 *drv);
uint16_t  drv8323_read_fault2(DRV8323 *drv);
esp_err_t drv8323_clear_faults(DRV8323 *drv);
