idf_component_register(SRCS 
                        "src/audio_i2c_bus.c"
                        "src/audio_i2s_bus.c"
                        "src/audio_out.c"
                        "src/es8311_raw.c"
                       INCLUDE_DIRS "include"
                       REQUIRES driver i2c_bus pca9557)