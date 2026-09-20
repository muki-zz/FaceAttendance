idf_component_register(
    SRCS "src/cloud_service.c"
    INCLUDE_DIRS "include"
    REQUIRES storage wifi_provisioning mqtt esp_netif esp_wifi
)
