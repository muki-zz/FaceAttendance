/**
 * @file i2c_bus.c
 * @brief i2c驱动实现
 */

#include "esp_check.h"
#include "i2c_bus.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

#define TAG "i2c_bus"



static i2c_bus_t buses[I2C_NUM_MAX];
static SemaphoreHandle_t guard;
static portMUX_TYPE init_guard = portMUX_INITIALIZER_UNLOCKED;

static void i2c_bus_init_once(void)
{
    taskENTER_CRITICAL(&init_guard);
    static bool inited = false;
    if (!inited)
    {
        guard = xSemaphoreCreateMutex();
        for (int i = 0; i < I2C_NUM_MAX; i++)
        {
            buses[i].port = I2C_NUM_0;
            buses[i].native_bus = NULL;
            buses[i].ready = false;
        }
        inited = true;
    }
    taskEXIT_CRITICAL(&init_guard);
}

esp_err_t i2c_bus_get(const i2c_bus_config_t *config, i2c_bus_handle_t *ret_bus)
{
    ESP_RETURN_ON_FALSE(config && ret_bus, ESP_ERR_INVALID_ARG, TAG, "config or output handle is NULL");
    ESP_RETURN_ON_FALSE(config->port >= I2C_NUM_0 && config->port < I2C_NUM_MAX,
                        ESP_ERR_INVALID_ARG, TAG, "invalid I2C port");
    ESP_RETURN_ON_FALSE(GPIO_IS_VALID_GPIO(config->sda_gpio_num) &&
                        GPIO_IS_VALID_GPIO(config->scl_gpio_num),
                        ESP_ERR_INVALID_ARG, TAG, "invalid I2C pins");
    i2c_bus_init_once();
    *ret_bus = NULL;

    i2c_bus_t *bus = &buses[config->port];
    if (bus->ready)
    {
        *ret_bus = bus;
        return ESP_OK;
    }

    i2c_master_bus_config_t bus_config = {
        .i2c_port = config->port,
        .sda_io_num = config->sda_gpio_num,
        .scl_io_num = config->scl_gpio_num,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = {
            .enable_internal_pullup = config->enable_internal_pullup,
        },
    };
    i2c_master_bus_handle_t native_bus = NULL;
    esp_err_t err = i2c_new_master_bus(&bus_config, &native_bus);
    ESP_RETURN_ON_ERROR(err, TAG, "i2c_new_master_bus fail");

    bus->port = config->port;
    bus->native_bus = native_bus;
    bus->ready = true;
    *ret_bus = bus;
    return ESP_OK;
}

// 对外接口：取出底层原生NG句柄，给pca9557 / touch使用
esp_err_t i2c_bus_get_native(i2c_bus_handle_t bus, i2c_master_bus_handle_t *native_bus)
{
    ESP_RETURN_ON_FALSE(bus && native_bus, ESP_ERR_INVALID_ARG, TAG, "null handle");
    ESP_RETURN_ON_FALSE(bus->ready, ESP_ERR_INVALID_STATE, TAG, "bus not ready");
    *native_bus = bus->native_bus;
    return ESP_OK;
}

esp_err_t i2c_bus_delete(i2c_bus_handle_t bus)
{
    // ESP_RETURN_ON_FALSE(bus, ESP_ERR_INVALID_ARG, TAG, "bus null");
    // if (bus->native_bus)
    // {
    //     i2c_master_bus_del(bus->native_bus);
    //     bus->native_bus = NULL;
    // }
    // bus->ready = false;
    return ESP_OK;
}