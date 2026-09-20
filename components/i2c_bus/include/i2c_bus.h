/**
 * @file i2c_bus.h
 * @brief i2c驱动声明
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"

// i2c总线实体句柄
typedef struct i2c_bus *i2c_bus_handle_t;

// i2c总线配置
typedef struct {
    i2c_port_t port;
    gpio_num_t sda_gpio_num;
    gpio_num_t scl_gpio_num;
    uint32_t clock_hz;
    bool enable_internal_pullup;
} i2c_bus_config_t;

// i2c总线实体
typedef struct i2c_bus {
    i2c_port_t port;
    i2c_master_bus_handle_t native_bus; 
    bool ready;
} i2c_bus_t;

/**
 * @brief 初始化i2c总线
 */
esp_err_t i2c_bus_get(const i2c_bus_config_t *config, i2c_bus_handle_t *ret_bus);
esp_err_t i2c_bus_get_native(i2c_bus_handle_t bus, i2c_master_bus_handle_t *native_bus);
esp_err_t i2c_bus_delete(i2c_bus_handle_t bus);


