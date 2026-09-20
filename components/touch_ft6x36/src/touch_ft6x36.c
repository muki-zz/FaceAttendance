#include <stdlib.h>
#include "touch_ft6x36.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"

#define FT_ADDR 0x38
struct touch_ft6x36 {
    touch_ft6x36_config_t config;
    i2c_master_dev_handle_t device;
    SemaphoreHandle_t lock;
};

// 传输层：复用 PCA9557 已经安装的 I2C 总线，超时单位为 FreeRTOS tick。
static esp_err_t read_reg(touch_ft6x36_handle_t t, uint8_t reg, uint8_t *data, size_t size)
{
    return i2c_master_transmit_receive(t->device, &reg, 1, data, size, 100);
}
static esp_err_t write_reg(touch_ft6x36_handle_t t, uint8_t reg, uint8_t value)
{
    const uint8_t data[] = {reg, value};
    return i2c_master_transmit(t->device, data, sizeof(data), 100);
}

esp_err_t touch_ft6x36_create(const touch_ft6x36_config_t *config, touch_ft6x36_handle_t *out)
{
    if (!out) 
        return ESP_ERR_INVALID_ARG;

    *out = NULL;

    if (!config || !config->bus || !config->scl_speed_hz || !config->raw_width ||
        !config->raw_height || config->raw_width > 4096 || config->raw_height > 4096)
        return ESP_ERR_INVALID_ARG;

    touch_ft6x36_handle_t t = calloc(1, sizeof(*t));
    if (!t) 
        return ESP_ERR_NO_MEM;

    t->config = *config;
    t->lock = xSemaphoreCreateMutex();

    if (!t->lock) 
    { 
        free(t); 
        return ESP_ERR_NO_MEM; 
    }

    const i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = FT_ADDR,
        .scl_speed_hz = config->scl_speed_hz,
    };
    esp_err_t err = i2c_master_bus_add_device(config->bus, &device_config, &t->device);
    if (err != ESP_OK) {
        touch_ft6x36_delete(t);
        return err;
    }

    // 等待上电/系统复位完成。共享 RESET 不能在这里再次拉低。
    vTaskDelay(pdMS_TO_TICKS(200));
    uint8_t chip = 0, vendor = 0;

    err = read_reg(t, 0xA3, &chip, 1);
    if (err == ESP_OK) err = read_reg(t, 0xA8, &vendor, 1);
    if (err == ESP_OK) {
        ESP_LOGI("ft6x36", "address=0x38 chip=0x%02x vendor=0x%02x", chip, vendor);
        // 与用户参考驱动支持的型号列表一致；未知设备不盲写寄存器。
        if (vendor != 0x11 || (chip != 0x06 && chip != 0x33 && chip != 0x36 && chip != 0x64))
            err = ESP_ERR_NOT_SUPPORTED;
    }

    if (err == ESP_OK) 
        err = write_reg(t, 0x00, 0x00); // 正常工作模式
    if (err == ESP_OK) 
        err = write_reg(t, 0xA4, 0x00); // 轮询模式

    // 保留固件的触摸阈值，不在缺少面板测量时改变灵敏度。
    if (err != ESP_OK) 
    { 
        touch_ft6x36_delete(t); 
        return err; 
    }

    *out = t;
    return ESP_OK;
}

esp_err_t touch_ft6x36_read(touch_ft6x36_handle_t t, touch_ft6x36_sample_t *sample)
{
    if (!sample)
        return ESP_ERR_INVALID_ARG;

    *sample = (touch_ft6x36_sample_t){0};

    if (!t) 
        return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(t->lock, portMAX_DELAY);

    // 0x02=TD_STATUS，随后每触点6字节：XH XL YH YL WEIGHT MISC。
    uint8_t data[13] = {0};
    esp_err_t err = read_reg(t, 0x02, data, sizeof(data));

    touch_ft6x36_sample_t next = {0};
    if (err == ESP_OK && (data[0] & 0x0F) > 2) 
        err = ESP_ERR_INVALID_RESPONSE;
    
    if (err == ESP_OK) {
        for (unsigned i = 0; i < (data[0] & 0x0F); ++i) {
            const uint8_t *p = &data[1 + 6 * i];
            uint8_t event = p[0] >> 6;
            if (event == 1 || event == 3) 
                continue; // 抬起/保留事件不视为按下
            
            uint16_t x = ((p[0] & 0x0F) << 8) | p[1];
            uint16_t y = ((p[2] & 0x0F) << 8) | p[3];
            
            if (x >= t->config.raw_width || y >= t->config.raw_height) {
                err = ESP_ERR_INVALID_RESPONSE;
                break;
            }
            
            touch_ft6x36_point_t point = {.raw_x=x, .raw_y=y, .id=p[2] >> 4};
            
            uint16_t width = t->config.raw_width, height = t->config.raw_height;
            if (t->config.swap_xy) {
                uint16_t temp = x; 
                x = y; 
                y = temp;
                temp = width; 
                width = height; 
                height = temp;
            }

            if (t->config.mirror_x) 
                x = width - 1 - x;
            
            if (t->config.mirror_y) 
                    y = height - 1 - y;
            
            point.x = x; 
            point.y = y;
            next.points[next.count++] = point;
        }
    }
    if (err == ESP_OK) 
        *sample = next;
    xSemaphoreGive(t->lock);
    return err;
}

esp_err_t touch_ft6x36_set_direction(touch_ft6x36_handle_t t, bool swap_xy, bool mirror_x, bool mirror_y)
{
    if (!t) 
        return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(t->lock, portMAX_DELAY);
    t->config.swap_xy = swap_xy;
    t->config.mirror_x = mirror_x;
    t->config.mirror_y = mirror_y;
    xSemaphoreGive(t->lock);
    return ESP_OK;
}

esp_err_t touch_ft6x36_set_mirror(touch_ft6x36_handle_t t, bool mirror_x, bool mirror_y)
{
    if (!t) 
        return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(t->lock, portMAX_DELAY);
    t->config.mirror_x = mirror_x;
    t->config.mirror_y = mirror_y;
    xSemaphoreGive(t->lock);
    return ESP_OK;
}

esp_err_t touch_ft6x36_delete(touch_ft6x36_handle_t t)
{
    if (t) {
        if (t->device) i2c_master_bus_rm_device(t->device);
        if (t->lock) vSemaphoreDelete(t->lock);
        free(t);
    }
    return ESP_OK; // 公共 I2C 总线由板级层持有，本组件不删除
}
