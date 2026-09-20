#include "st7789_panel.h"       // 引入 ST7789 面板接口声明。
#include "esp_check.h"          // 引入 ESP-IDF 错误检查宏。
#include "esp_lcd_io_spi.h"         // 引入 SPI LCD 面板 IO 接口。
#include "esp_lcd_panel_vendor.h" // 引入 ST7789 面板驱动接口。

esp_err_t st7789_panel_create(const lcd_display_config_t *config, st7789_panel_t *ret_panel) // 创建并初始化 ST7789 面板。
{ 
    ESP_RETURN_ON_FALSE(config && ret_panel, ESP_ERR_INVALID_ARG, "st7789", "bad argument"); // 检查配置和输出句柄是否有效。
    esp_lcd_panel_io_spi_config_t io_cfg = { // 创建 SPI 面板 IO 配置。
        .cs_gpio_num = config->cs_via_pca9557 ? -1 : config->cs_gpio_num, // PCA9557 IO0 时不让 SPI 控制 CS。
        .dc_gpio_num = config->dc_gpio_num, // 设置数据或命令选择信号 GPIO。
        .spi_mode = 0, // 使用 SPI 模式 0。
        .pclk_hz = config->pclk_hz, // 设置 SPI 时钟频率。
        .trans_queue_depth = 10, // 设置待处理 SPI 事务队列深度。
        .lcd_cmd_bits = 8, // 设置 LCD 命令长度为 8 位。
        .lcd_param_bits = 8, // 设置 LCD 参数长度为 8 位。
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(config->spi_host, &io_cfg, &ret_panel->io), "st7789", "create panel IO failed"); // 创建 SPI 面板 IO 对象。

    esp_lcd_panel_dev_config_t panel_cfg = { // 创建 ST7789 面板设备配置。
        .reset_gpio_num = config->reset_gpio_num, // 设置面板复位信号 GPIO。
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR, // 保留已验证的 BGR 分量顺序。
        .data_endian = LCD_RGB_DATA_ENDIAN_BIG, // 设置像素数据使用大端字节序。
        .bits_per_pixel = 16, // 设置每个像素使用 16 位 RGB565 数据。
    };
    esp_err_t err = esp_lcd_new_panel_st7789(ret_panel->io, &panel_cfg, &ret_panel->panel); // 创建 ST7789 面板驱动对象。
    if (err != ESP_OK) { // 判断面板对象创建是否失败。
        /* 已创建的IO交给调用者统一清理，避免释放失败时丢失句柄。 */
        return err; // 返回面板创建错误。
    }
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(ret_panel->panel), "st7789", "reset failed"); // 复位 ST7789 面板。
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(ret_panel->panel), "st7789", "init failed"); // 初始化 ST7789 面板。
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(ret_panel->panel, config->x_gap, config->y_gap), "st7789", "set gap failed"); // 设置显示区域的横向和纵向偏移。
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(ret_panel->panel, config->swap_xy), "st7789", "swap xy failed"); // 根据配置决定是否交换横纵坐标。
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(ret_panel->panel, config->mirror_x, config->mirror_y), "st7789", "mirror failed"); // 根据配置设置横向和纵向镜像。
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(ret_panel->panel, config->invert_color), "st7789", "invert failed"); // 根据配置设置颜色反转。
    return esp_lcd_panel_disp_on_off(ret_panel->panel, true); // 打开面板显示并返回操作结果。
}

esp_err_t st7789_panel_delete(st7789_panel_t *panel)
{
    if (!panel) return ESP_OK;
    if (panel->panel) {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_del(panel->panel), "st7789", "delete panel");
        panel->panel = NULL;
    }
    if (panel->io) {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_del(panel->io), "st7789", "delete IO");
        panel->io = NULL;
    }
    return ESP_OK;
}
