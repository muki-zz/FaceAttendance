#pragma once
#include <stddef.h>
#include "i2c_bus.h"
typedef struct {
    i2c_bus_handle_t handle; // Borrowed persistent bus.
} audio_ctrl_bus_t;
esp_err_t audio_reg_read(audio_ctrl_bus_t *bus, uint8_t addr, uint8_t reg, uint8_t *value);
esp_err_t audio_reg_write(audio_ctrl_bus_t *bus, uint8_t addr, uint8_t reg, uint8_t value);
esp_err_t audio_reg_update(audio_ctrl_bus_t *bus, uint8_t addr, uint8_t reg, uint8_t mask, uint8_t value);
