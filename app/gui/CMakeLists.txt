idf_component_register(
    SRCS "src/app_gui.c"
         "src/app_lvgl.c"
         "src/home_page.c"
         "src/register_page.c"
         "src/record_page.c"
         "src/stat_page.c"
         "src/schedule_page.c"
         "src/gui_common.c"
    INCLUDE_DIRS "include"
    REQUIRES lcd_st7789 bsp touch_ft6x36 lvgl esp_timer face storage cloud
)
