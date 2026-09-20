#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_netif_ip_addr.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIFI_PROV_STATE_STOPPED,
    WIFI_PROV_STATE_CONNECTING,
    WIFI_PROV_STATE_PROVISIONING,
    WIFI_PROV_STATE_CONNECTED,
} wifi_prov_state_t;

typedef struct {
    const char *ap_ssid_prefix;       /* 默认 ESP32S3，实际SSID带MAC后缀。 */
    const char *ap_password;          /* 8~63字符；NULL或空字符串表示开放热点。 */
    uint8_t sta_max_retry;            /* 连接失败多少次后开启配网页面，默认5。 */
    bool stop_ap_after_connected;     /* STA连接成功后是否关闭热点及HTTP服务。 */
} wifi_provisioning_config_t;

typedef struct {
    wifi_prov_state_t state;
    bool credentials_saved;
    char sta_ssid[33];
    char ap_ssid[33];
    esp_ip4_addr_t ip;
} wifi_provisioning_status_t;

/* NULL使用默认配置。函数非阻塞，后续连接、重连及配网由事件回调处理。 */
esp_err_t wifi_provisioning_start(const wifi_provisioning_config_t *config);
esp_err_t wifi_provisioning_get_status(wifi_provisioning_status_t *status);

/* 删除NVS中的SSID/密码并立即回到SoftAP配网模式。 */
esp_err_t wifi_provisioning_reset(void);

#ifdef __cplusplus
}
#endif
