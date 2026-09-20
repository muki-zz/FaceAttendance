idf_component_register(
    SRCS "src/face_service.c"
         "src/camera_capture.c"
         "src/camera_preview.c"
         "src/face_infer.cpp"
    INCLUDE_DIRS "include"
    REQUIRES camera i2c_bus pca9557 lcd_st7789 bsp storage cloud esp_timer
             esp-dl human_face_detect human_face_recognition
)
