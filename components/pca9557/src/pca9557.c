/**
 * @file pca9557.c
 * @brief pca9557驱动实现
 */

#include "pca9557.h"
#include "esp_check.h"
#include "esp_err.h"
#include <stdlib.h>

struct pca9557_t {
    uint8_t address;
    i2c_master_dev_handle_t device; // 设备句柄
};


enum { PCA_INPUT = 0x00, PCA_OUTPUT = 0x01, PCA_DIRECTION = 0x03 };

/**
 * @brief 主机读从机
 */
static esp_err_t pca9557_read_reg(pca9557_handle_t expander, uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(expander->device, &reg, sizeof(reg),
                                       value, sizeof(*value), 100);
}

/**
 * @brief 主机写从机
 */
static esp_err_t pca9557_write_reg(const pca9557_handle_t expander, uint8_t reg, uint8_t value)
{
    uint8_t data[] = { reg, value };
    return i2c_master_transmit(expander->device, data, sizeof(data), 100);
}

/**
 * @brief 寄存器位修改：读改写 RMW
*/ 
static esp_err_t pca9557_update_reg(pca9557_handle_t expander, uint8_t reg, uint8_t mask, uint8_t val)
{
    uint8_t old;
    esp_err_t ret = pca9557_read_reg(expander, reg, &old);
    if(ret != ESP_OK) return ret;
    uint8_t new_val = (old & (~mask)) | (val & mask);
    return pca9557_write_reg(expander, reg, new_val);
}

/**
 * @brief 设置cs片选引脚电平
 */
esp_err_t pca9557_set_lcd_cs(pca9557_handle_t expander, bool selected)
{
    ESP_RETURN_ON_FALSE(expander, ESP_ERR_INVALID_ARG, "pca9557", "null expander");
    uint8_t output = 0;
    ESP_RETURN_ON_ERROR(pca9557_read_reg(expander, PCA_OUTPUT, &output), "pca9557", "read output failed");
    if (selected) output &= (uint8_t)~(1U << 0);
    else output |= (1U << 0);
    return pca9557_write_reg(expander, PCA_OUTPUT, output);
}


// 设置引脚电平
esp_err_t pca9557_set_level(pca9557_handle_t d, uint8_t pin, bool high)
{
    ESP_RETURN_ON_FALSE(d && pin < 8, ESP_ERR_INVALID_ARG, "pca9557", "invalid device/pin");
    const uint8_t mask = (uint8_t)(1u << pin);
    uint8_t val = high ? mask : 0;
    return pca9557_update_reg(d, PCA_OUTPUT, mask, val);
}

// 配置为输出，设置初始电平
esp_err_t pca9557_config_output(pca9557_handle_t d, uint8_t pin, bool initial_high)
{
    ESP_RETURN_ON_FALSE(d && pin < 8, ESP_ERR_INVALID_ARG, "pca9557", "invalid device/pin");
    ESP_RETURN_ON_ERROR(pca9557_set_level(d, pin, initial_high), "pca9557", "set latch failed");
    const uint8_t mask = (uint8_t)(1u << pin);
    // direction寄存器：0=输出
    return pca9557_update_reg(d, PCA_DIRECTION, mask, 0);
}

// 配置为输入
esp_err_t pca9557_config_input(pca9557_handle_t d, uint8_t pin)
{
    ESP_RETURN_ON_FALSE(d && pin < 8, ESP_ERR_INVALID_ARG, "pca9557", "invalid device/pin");
    const uint8_t mask = (uint8_t)(1u << pin);
    // direction寄存器：1=输入
    return pca9557_update_reg(d, PCA_DIRECTION, mask, mask);
}


esp_err_t pca9557_create(i2c_bus_handle_t bus, uint8_t address, pca9557_handle_t *out)
{
    //
    ESP_RETURN_ON_FALSE(out, ESP_ERR_INVALID_ARG, "pca9557", "null output");
    *out = NULL;
    ESP_RETURN_ON_FALSE(bus && address >= 0x18 && address <= 0x1f, ESP_ERR_INVALID_ARG, "pca9557", "invalid bus/address");
    
    // 1. 取出原生I2C总线句柄
    i2c_master_bus_handle_t native_bus;
    ESP_RETURN_ON_ERROR(i2c_bus_get_native(bus, &native_bus), "pca9557", "get native bus fail");

     // 2. 分配内存
    pca9557_handle_t ret_expander = calloc(1, sizeof(struct pca9557_t));
    ESP_RETURN_ON_FALSE(ret_expander, ESP_ERR_NO_MEM, "pca9557", "malloc fail");
    ret_expander->address = address;

    // 3. 创建设备句柄
    i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = 100000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(native_bus, &device_config,
                                                  &ret_expander->device),
                        "pca9557", "add I2C device failed");
    
     // 初始化：LCD_CS默认拉高（不选中）
    esp_err_t err = pca9557_set_lcd_cs(ret_expander, false);
    if (err == ESP_OK)
    {
        uint8_t direction = 0;
        err = pca9557_read_reg(ret_expander, PCA_DIRECTION, &direction);
        if (err == ESP_OK)
        {
            // IO0 设置为输出
            direction &= (uint8_t)~(1U << 0);
            err = pca9557_write_reg(ret_expander, PCA_DIRECTION, direction);
        }
    }

    if (err != ESP_OK) {
        i2c_master_bus_rm_device(ret_expander->device);
        free(ret_expander);
        return err;
    }
    *out = ret_expander;
    return ESP_OK;
}

void pca9557_delete(pca9557_handle_t d)
{
    free(d);
}

