#include "audio_i2c_bus.h"
// Compatibility adapter only: initialization/state/locking belong to i2c_bus.c.
esp_err_t audio_reg_read(audio_ctrl_bus_t *b, uint8_t addr, uint8_t reg, uint8_t *value)
{
    return b ? i2c_bus_read_reg(b->handle, addr, reg, value) : ESP_ERR_INVALID_ARG;
}
esp_err_t audio_reg_write(audio_ctrl_bus_t *b, uint8_t addr, uint8_t reg, uint8_t value)
{
    return b ? i2c_bus_write_reg(b->handle, addr, reg, value) : ESP_ERR_INVALID_ARG;
}
esp_err_t audio_reg_update(audio_ctrl_bus_t *b, uint8_t addr, uint8_t reg, uint8_t mask, uint8_t value)
{
    return b ? i2c_bus_update_reg(b->handle, addr, reg, mask, value) : ESP_ERR_INVALID_ARG;
}
