#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "driver/spi_master.h"

typedef struct {
    spi_host_device_t host;
    bool owns_bus;
} lcd_spi_bus_t;

esp_err_t lcd_spi_bus_create(spi_host_device_t host, int sclk_gpio, int mosi_gpio,
                             int max_transfer_bytes, lcd_spi_bus_t *ret_bus);
esp_err_t lcd_spi_bus_delete(lcd_spi_bus_t *bus);
