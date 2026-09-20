idf_component_register(
    SRCS "src/i2c_bus.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_i2c driver
)
