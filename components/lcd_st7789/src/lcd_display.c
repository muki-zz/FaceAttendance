#include <stdlib.h> // 提供 calloc 和 free 等内存管理函数。
#include "freertos/FreeRTOS.h" // 提供 FreeRTOS 的基础类型和常量。
#include "freertos/semphr.h" // 提供互斥锁信号量接口。
#include "driver/gpio.h" // 提供 GPIO 配置和电平控制接口。
#include "esp_check.h" // 提供 ESP-IDF 的参数和错误检查宏。
#include "esp_heap_caps.h" // 提供支持 DMA 能力的堆内存分配接口。
#include "lcd_display.h" // 引入 LCD 显示封装层的公开声明。
#include "lcd_spi_bus.h" // 引入 LCD SPI 总线的内部接口。
#include "pca9557.h" // 使用audio工程的共用pac9557组件。
#include "st7789_panel.h" // 引入 ST7789 面板的内部接口。

struct lcd_display_t {
    lcd_display_config_t config; // 保存 LCD 分辨率、GPIO 和 SPI 配置。
    lcd_spi_bus_t bus; // 保存 SPI 总线资源及其状态。
    st7789_panel_t st7789; // 保存 ST7789 面板和面板通信资源。
    bool cs_touched; // 记录IO0可能已被修改，退出时恢复高电平。
    bool closing; // 删除失败后只允许重试删除。
    bool backlight_ready;
    SemaphoreHandle_t lock; // 保护显示操作并发访问的互斥锁。
};

esp_err_t lcd_display_create(const lcd_display_config_t *config, lcd_display_handle_t *ret_display) // 创建 LCD 显示对象。
{
    ESP_RETURN_ON_FALSE(ret_display, ESP_ERR_INVALID_ARG, "lcd", "null output");
    *ret_display = NULL;
    ESP_RETURN_ON_FALSE(config && config->hor_res && config->ver_res,
                        ESP_ERR_INVALID_ARG, "lcd", "bad config");
    ESP_RETURN_ON_FALSE(!config->cs_via_pca9557 ||
                        (config->expander && config->cs_gpio_num == -1),
                        ESP_ERR_INVALID_ARG, "lcd", "shared expander required; CS GPIO must be -1");

    esp_err_t ret = ESP_OK; // 初始化错误码，供失败清理路径返回。
    struct lcd_display_t *display = calloc(1, sizeof(*display)); // 分配并清零显示对象。

    ESP_RETURN_ON_FALSE(display, ESP_ERR_NO_MEM, "lcd", "no memory"); // 内存分配失败时返回无内存错误。

    display->config = *config; // 保存调用者提供的显示配置。
    display->lock = xSemaphoreCreateMutex(); // 创建保护显示操作的互斥锁。
    if (!display->lock) { 
        free(display); 
        return ESP_ERR_NO_MEM; 
    } // 互斥锁创建失败时释放对象并返回错误。

    /* 先成功申请独占SPI，再修改CS，避免第二个实例干扰已有LCD。 */
    ESP_GOTO_ON_ERROR(lcd_spi_bus_create(
        config->spi_host, config->sclk_gpio_num, config->mosi_gpio_num,
        (config->hor_res > config->ver_res ? config->hor_res : config->ver_res)
            * 40 * sizeof(uint16_t),
        &display->bus), fail, "lcd", "SPI bus failed");

    if (config->cs_via_pca9557) {
        display->cs_touched = true;
        ESP_GOTO_ON_ERROR(pca9557_config_output(config->expander, 0, true),
                          fail, "lcd", "configure LCD CS");
        ESP_GOTO_ON_ERROR(pca9557_set_level(config->expander, 0, false),
                          fail, "lcd", "select LCD");
    }

    ESP_GOTO_ON_ERROR(st7789_panel_create(config, &display->st7789), fail, "lcd", "panel failed"); // 创建并初始化 ST7789 面板对象。

    if (config->backlight_gpio_num >= 0) { // 仅在配置了有效背光 GPIO 时初始化背光。
        gpio_config_t bl_cfg = { .pin_bit_mask = 1ULL << config->backlight_gpio_num, .mode = GPIO_MODE_OUTPUT }; // 创建背光 GPIO 的输出配置。
        ESP_GOTO_ON_ERROR(gpio_config(&bl_cfg), fail, "lcd", "backlight GPIO failed"); // 应用背光 GPIO 配置。
        display->backlight_ready = true;
        ESP_GOTO_ON_ERROR(lcd_display_set_backlight(display, true), fail, "lcd", "backlight failed"); // 默认打开背光。
    }
    *ret_display = display; // 将创建好的显示对象句柄返回给调用者。
    
    return ESP_OK; // 返回创建成功。

fail: // 统一处理创建过程中的失败情况。
    ESP_ERROR_CHECK(lcd_display_delete(display)); // 回滚失败不能丢失资源上下文。
    return ret; // 返回触发跳转的错误码。
}

// Caller holds display->lock for the entire operation, including DMA completion.
static esp_err_t draw_locked(lcd_display_handle_t display, int x_start, int y_start, int x_end, int y_end, const void *color_data)
{
    ESP_RETURN_ON_FALSE(display && color_data && x_start >= 0 && y_start >= 0 && x_end > x_start && y_end > y_start && // 检查句柄、像素数据以及绘制区域参数。
                        x_end <= display->config.hor_res && y_end <= display->config.ver_res, ESP_ERR_INVALID_ARG, "lcd", "invalid rectangle"); // 确保绘制区域不超出 LCD 边界。
    esp_err_t err = esp_lcd_panel_draw_bitmap(display->st7789.panel, x_start, y_start, x_end, y_end, color_data); // 将位图提交给 ST7789 面板。
    // tx_color is queued by esp_lcd. A no-command parameter transfer waits for that queue,
    // so callers may safely reuse color_data once this facade function returns.
    if (err == ESP_OK) err = esp_lcd_panel_io_tx_param(display->st7789.io, -1, NULL, 0); // 等待此前排队的像素传输完成。
    return err; // 返回位图绘制或等待传输的结果。
}

esp_err_t lcd_display_draw_bitmap(lcd_display_handle_t display, int x_start, int y_start, int x_end, int y_end, const void *color_data)
{
    if (!display || display->closing) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(display->lock, portMAX_DELAY);
    esp_err_t err = draw_locked(display, x_start, y_start, x_end, y_end, color_data);
    xSemaphoreGive(display->lock);
    return err;
}

esp_err_t lcd_display_set_direction(lcd_display_handle_t display, bool swap_xy, bool mirror_x, bool mirror_y)
{
    if (!display || display->closing) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(display->lock, portMAX_DELAY);
    lcd_display_config_t next = display->config;
    if (next.swap_xy != swap_xy) {
        uint16_t tmp = next.hor_res;
        next.hor_res = next.ver_res;
        next.ver_res = tmp;
        tmp = next.x_gap;
        next.x_gap = next.y_gap;
        next.y_gap = tmp;
    }
    next.swap_xy = swap_xy;
    next.mirror_x = mirror_x;
    next.mirror_y = mirror_y;
    esp_lcd_panel_handle_t panel = display->st7789.panel;
    esp_err_t err = esp_lcd_panel_swap_xy(panel, swap_xy);
    if (err == ESP_OK) err = esp_lcd_panel_mirror(panel, mirror_x, mirror_y);
    if (err == ESP_OK) err = esp_lcd_panel_set_gap(panel, next.x_gap, next.y_gap);
    if (err == ESP_OK) {
        display->config = next;
    } else {
        // Restore the previous orientation if a command fails; report recovery failure.
        esp_err_t a = esp_lcd_panel_swap_xy(panel, display->config.swap_xy);
        esp_err_t b = esp_lcd_panel_mirror(panel, display->config.mirror_x, display->config.mirror_y);
        esp_err_t c = esp_lcd_panel_set_gap(panel, display->config.x_gap, display->config.y_gap);
        if (a != ESP_OK || b != ESP_OK || c != ESP_OK) {
            ESP_LOGE("lcd", "Direction recovery failed; recreate the display before drawing");
            err = ESP_ERR_INVALID_STATE;
        }
    }
    xSemaphoreGive(display->lock);
    return err;
}

esp_err_t lcd_display_get_size(lcd_display_handle_t display, uint16_t *width, uint16_t *height)
{
    if (!display || display->closing || !width || !height) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(display->lock, portMAX_DELAY);
    *width = display->config.hor_res;
    *height = display->config.ver_res;
    xSemaphoreGive(display->lock);
    return ESP_OK;
}

esp_err_t lcd_display_fill(lcd_display_handle_t display, uint16_t rgb565) // 使用指定颜色填充整个 LCD。
{
    ESP_RETURN_ON_FALSE(display && !display->closing, ESP_ERR_INVALID_STATE, "lcd", "display unavailable"); // 检查显示对象句柄是否有效。

    xSemaphoreTake(display->lock, portMAX_DELAY); // 整次填充期间禁止切换方向。

    size_t pixels = display->config.hor_res * 16; // 计算一次传输 16 行所需的像素数量。

    uint16_t *line = heap_caps_malloc(pixels * sizeof(*line), MALLOC_CAP_DMA); // 分配可供 DMA 使用的填充缓冲区。

    if (!line) {
        xSemaphoreGive(display->lock);
        return ESP_ERR_NO_MEM;
    }

    uint8_t *bytes = (uint8_t *)line;

    for (size_t i = 0; i < pixels; ++i) {
        bytes[2 * i]     = (uint8_t)(rgb565 >> 8); // 高字节先发送
        bytes[2 * i + 1] = (uint8_t)rgb565;        // 低字节后发送
    }

    esp_err_t err = ESP_OK; // 初始化填充操作的错误码。

    // 按每次 16 行的批次遍历整个屏幕。
    for (int y = 0; y < display->config.ver_res && err == ESP_OK; y += 16) { 
        int y_end = (y + 16 < display->config.ver_res) ? y + 16 : display->config.ver_res; // 计算当前批次的结束行并处理屏幕尾部。
        err = draw_locked(display, 0, y, display->config.hor_res, y_end, line); // 已持锁，不重复获取互斥锁。
    }
    free(line); // 释放 DMA 填充缓冲区。
    xSemaphoreGive(display->lock);
    return err; // 返回整屏填充结果。
}

esp_err_t lcd_display_set_backlight(lcd_display_handle_t display, bool on) // 设置 LCD 背光开关状态。
{
    ESP_RETURN_ON_FALSE(display && !display->closing, ESP_ERR_INVALID_STATE, "lcd", "display unavailable"); // 检查显示对象句柄是否有效。
    if (display->config.backlight_gpio_num < 0) return ESP_OK; // 未配置背光 GPIO 时直接视为成功。
    return gpio_set_level(display->config.backlight_gpio_num, on == display->config.backlight_active_high); // 根据有效电平配置设置背光 GPIO 电平。
} 

esp_err_t lcd_display_delete(lcd_display_handle_t display)
{
    if (!display) return ESP_OK;
    /* 调用者必须已停止所有显示任务；此接口不与绘图并发。 */
    display->closing = true;
    if (display->backlight_ready) {
        ESP_RETURN_ON_ERROR(gpio_set_level(display->config.backlight_gpio_num,
                            !display->config.backlight_active_high), "lcd", "backlight off");
        display->backlight_ready = false;
    }
    /* IO删除等待在途SPI传输后，才释放片选。 */
    ESP_RETURN_ON_ERROR(st7789_panel_delete(&display->st7789), "lcd", "panel cleanup");
    if (display->cs_touched) {
        ESP_RETURN_ON_ERROR(pca9557_config_output(display->config.expander, 0, true),
                            "lcd", "release LCD CS; keep handle and retry");
        display->cs_touched = false;
    }
    ESP_RETURN_ON_ERROR(lcd_spi_bus_delete(&display->bus), "lcd", "SPI cleanup");
    if (display->lock) vSemaphoreDelete(display->lock);
    free(display);
    return ESP_OK;
}
