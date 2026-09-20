idf_component_register(SRCS "src/app_ctx.c"
                       PRIV_REQUIRES
                       INCLUDE_DIRS "include"
                       REQUIRES i2c_bus pca9557 lcd_st7789 touch_ft6x36 camera
                       )
