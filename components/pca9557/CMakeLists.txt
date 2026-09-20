idf_component_register(SRCS 
                        "src/board_pa.c" "src/pca9557.c"
                       INCLUDE_DIRS "include"
                       REQUIRES driver i2c_bus)