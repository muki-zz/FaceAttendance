idf_component_register(
    SRCS "src/storage.c" "src/nvs_cfg.c" "src/timetable.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES spiffs nvs_flash
    EMBED_TXTFILES "attendance_config.default.json"
)
