#include "app_ctx.h"
#include <stdlib.h>
#include <string.h>
#include "esp_check.h"
static const char *TAG = "app_ctx";





// IIC 配置
#define I2C_SDA_GPIO GPIO_NUM_1
#define I2C_SCL_GPIO GPIO_NUM_2
#define I2C_PORT     I2C_NUM_0
#define I2C_SPEED    100000 

// PCA9557配置
#define I2C_PCA9557_ADDR 0x19U

// LCD屏配置
#define LCD_SPI_HOST    SPI2_HOST
#define LCD_SCLK_GPIO   GPIO_NUM_41
#define LCD_MOSI_GPIO   GPIO_NUM_40

#define LCD_DC_GPIO GPIO_NUM_39       // U12 LCD_RS: IO39_LCD_DC
#define LCD_RESET_GPIO -1             // U12 LCD_RST 接 RESET 网络，未连接到可控 ESP GPIO

#define LCD_BACKLIGHT_GPIO GPIO_NUM_42 // U12 LED: IO42_LCD_BL
#define LCD_HOR_RES 480 
#define LCD_VER_RES 320
#define LCD_PCLK_HZ (40 * 1000 * 1000)

esp_err_t app_ctx_create(app_ctx_t *ctx)
{
    ESP_RETURN_ON_FALSE(ctx, ESP_ERR_INVALID_ARG, TAG, "ctx is NULL");
    esp_err_t ret = ESP_OK;
    memset(ctx, 0, sizeof(app_ctx_t));

    // 初始化 IIC 总线
    const i2c_bus_config_t i2c_config = {
        .port = I2C_PORT, .sda_gpio_num = I2C_SDA_GPIO, .scl_gpio_num = I2C_SCL_GPIO,
        .enable_internal_pullup = true
    };
    ESP_GOTO_ON_ERROR(i2c_bus_get(&i2c_config,&ctx->i2c_bus), fail, TAG, "i2c bus init");
    // 初始化 PCA9557
    ESP_GOTO_ON_ERROR(pca9557_create(ctx->i2c_bus, I2C_PCA9557_ADDR, &ctx->io_expander), fail, TAG, "pca9557 init");
    // 初始化LCD
    const lcd_display_config_t lcd_config = {
        .spi_host = LCD_SPI_HOST, 
        .sclk_gpio_num = LCD_SCLK_GPIO, 
        .mosi_gpio_num = LCD_MOSI_GPIO,
        .cs_gpio_num = -1, // LCD_CS 通过 PCA9557PW IO0 控制，不能填写 ESP GPIO。
        .dc_gpio_num = LCD_DC_GPIO, 
        .reset_gpio_num = LCD_RESET_GPIO, 
        .backlight_gpio_num = LCD_BACKLIGHT_GPIO,
        .cs_via_pca9557 = true,
        .expander = {
            ctx->io_expander
        },
        .hor_res = LCD_HOR_RES, 
        .ver_res = LCD_VER_RES, 
        .pclk_hz = LCD_PCLK_HZ,
        .x_gap = 0, 
        .y_gap = 0, 
        .swap_xy = true, 
        .mirror_x = true, 
        .mirror_y = true,
        .invert_color = true, 
        .backlight_active_high = true,
    };
    ESP_GOTO_ON_ERROR(lcd_display_create(&lcd_config, &ctx->lcd), fail, TAG, "lcd create");
    i2c_master_bus_handle_t native_bus;
    ESP_RETURN_ON_ERROR(i2c_bus_get_native(ctx->i2c_bus, &native_bus), TAG, "get native bus fail");
    
    //初始化触摸屏
    const touch_ft6x36_config_t touch_config = {
        .scl_speed_hz = 10000,
        .bus = native_bus, 
        .raw_width = 320, 
        .raw_height = 480,
        .swap_xy = true,  
        .mirror_x = false, 
        .mirror_y = true
    };
    ESP_GOTO_ON_ERROR(touch_ft6x36_create(&touch_config,&ctx->touch_device), fail, TAG, "touch create");

    //初始化摄像头
    // camera_device_config_t camera_config;
    // camera_device_default_config(&camera_config);
    // camera_config.pixel_format = PIXFORMAT_RGB565;
    // camera_config.frame_size = FRAMESIZE_QVGA;
    // ESP_GOTO_ON_ERROR(
    //     camera_device_open(
    //         &ctx->camera,
    //         ctx->i2c_bus,
    //         ctx->io_expander,
    //         &camera_config),
    //     fail, TAG, "camera open failed");
    // ESP_LOGI(TAG, "preview started");

    // LVGL flush and camera preview share the same LCD component.
    // Serialize access at the application layer without changing component code.
    ctx->lcd_mutex = xSemaphoreCreateMutex();
    ESP_GOTO_ON_FALSE(ctx->lcd_mutex != NULL, ESP_ERR_NO_MEM, fail, TAG, "lcd mutex create");

    return ESP_OK;

fail:
    // 初始化失败，销毁已经创建的资源
    app_ctx_delete(ctx);
    return ret;
}

void app_ctx_delete(app_ctx_t *ctx)
{
    if (!ctx) return;

    // 反向顺序释放资源
    // if(ctx->lcd) lcd_display_delete(ctx->lcd);
    // if(ctx->io_expander) pca9557_delete(ctx->io_expander);
    // if(ctx->i2c_bus) i2c_bus_delete(ctx->i2c_bus);

    if(ctx->lcd_mutex) {
        vSemaphoreDelete(ctx->lcd_mutex);
        ctx->lcd_mutex = NULL;
    }

    memset(ctx, 0, sizeof(app_ctx_t));
}