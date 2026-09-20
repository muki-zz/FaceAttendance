#include "lcd_spi_bus.h" // 引入 LCD SPI 总线接口声明。
#include "esp_check.h" // 引入 ESP-IDF 参数检查宏。

esp_err_t lcd_spi_bus_create(spi_host_device_t host, int sclk_gpio, int mosi_gpio, // 创建并初始化 LCD 使用的 SPI 总线。
                             int max_transfer_bytes, lcd_spi_bus_t *ret_bus) // 接收最大传输长度和创建结果句柄。
{
    ESP_RETURN_ON_FALSE(ret_bus && max_transfer_bytes > 0, ESP_ERR_INVALID_ARG, "lcd_bus", "bad argument"); // 检查输出句柄和最大传输长度是否有效。
    spi_bus_config_t cfg = { // 创建 SPI 总线配置结构体。
        .sclk_io_num = sclk_gpio, // 设置 SPI 时钟信号 GPIO。
        .mosi_io_num = mosi_gpio, // 设置 SPI 主机输出从机输入 GPIO。
        .miso_io_num = -1, // LCD 不使用主机输入从机输出信号
        .quadwp_io_num = -1, // 不使用 Quad SPI 写保护信号。
        .quadhd_io_num = -1, // 不使用 Quad SPI 保持信号。
        .max_transfer_sz = max_transfer_bytes, // 设置单次 SPI 传输允许的最大字节数。
    }; 
    esp_err_t err = spi_bus_initialize(host, &cfg, SPI_DMA_CH_AUTO); // 使用自动 DMA 通道初始化 SPI 总线。
    if (err != ESP_OK) { // 判断 SPI 总线初始化是否失败。
        return err; // 返回初始化错误，例如总线已被其他组件占用。
    }

    *ret_bus = (lcd_spi_bus_t){ .host = host, .owns_bus = true }; // 保存 SPI 主机编号并记录当前对象拥有总线。
    return ESP_OK; // 返回 SPI 总线创建成功。
} // 结束 SPI 总线创建函数。

esp_err_t lcd_spi_bus_delete(lcd_spi_bus_t *bus) // 删除 LCD SPI 总线并释放其资源。
{ // 开始执行 SPI 总线删除流程。
    if (!bus || !bus->owns_bus) return ESP_OK; // 空句柄或不拥有总线时无需释放。
    esp_err_t err = spi_bus_free(bus->host); // 释放指定 SPI 主机占用的资源。
    if (err == ESP_OK) bus->owns_bus = false; // 释放成功后清除总线所有权标记。
    return err; // 返回 SPI 总线释放结果。
} // 结束 SPI 总线删除函数。
