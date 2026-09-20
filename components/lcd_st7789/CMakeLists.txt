idf_component_register(
    SRCS "src/lcd_spi_bus.c" "src/st7789_panel.c" "src/lcd_display.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_lcd driver freertos esp_driver_gpio pca9557
)
