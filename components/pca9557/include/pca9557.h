/**
 * @file pca9557.h
 * @brief pca9557驱动声明
*/

#pragma once
#include "i2c_bus.h"

// 外设句柄
typedef struct pca9557_t *pca9557_handle_t;

// 配置参数
typedef struct pca9557_config_t{
    i2c_master_bus_handle_t bus;
    uint8_t address;
}pca9557_config_t;

esp_err_t pca9557_create(i2c_bus_handle_t bus, uint8_t address, pca9557_handle_t *out);
void pca9557_delete(pca9557_handle_t device);
esp_err_t pca9557_set_level(pca9557_handle_t device, uint8_t pin, bool high);
esp_err_t pca9557_config_output(pca9557_handle_t device, uint8_t pin, bool initial_high);
esp_err_t pca9557_config_input(pca9557_handle_t device, uint8_t pin);
esp_err_t pca9557_read_port(pca9557_handle_t device, uint8_t *value);
esp_err_t pca9557_set_lcd_cs(pca9557_handle_t expander, bool selected);
