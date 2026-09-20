#include "wifi_provisioning.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#define TAG "wifi_prov"
#define NVS_NAMESPACE "wifi_prov"
#define NVS_KEY_SSID "ssid"
#define NVS_KEY_PASSWORD "password"
#define POST_MAX_BYTES 256
#define PORTAL_WAIT_INTERVAL_MS 100
#define PORTAL_WAIT_MAX_ATTEMPTS 50
#define PORTAL_TASK_STACK_SIZE 4096
#define PORTAL_TASK_PRIORITY 3

#define WIFI_SID "FaceAttend"
#define WIFI_PASSWORD "88888888"

static const char s_page[] =
    "<!doctype html><html lang='zh-CN'><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>ESP32-S3 配网</title><style>body{font-family:sans-serif;max-width:"
    "420px;margin:40px auto;padding:0 18px}input,button{box-sizing:border-box;"
    "width:100%;padding:12px;margin:7px 0;font-size:16px}button{background:#1769aa;"
    "color:white;border:0;border-radius:5px}</style></head><body><h2>ESP32-S3 "
    "Wi-Fi 配网</h2><form method='post' action='/configure'><label>路由器 SSID"
    "</label><input name='ssid' maxlength='32' required><label>Wi-Fi 密码</label>"
    "<input name='password' type='password' maxlength='64'><button type='submit'>"
    "保存并连接</button></form></body></html>";

typedef struct {
    SemaphoreHandle_t lock;
    esp_netif_t *sta_netif;
    esp_netif_t *ap_netif;
    httpd_handle_t server;
    esp_event_handler_instance_t wifi_handler;
    esp_event_handler_instance_t ip_handler;
    wifi_provisioning_config_t config;
    wifi_provisioning_status_t status;
    uint8_t retry_count;
    bool portal_start_pending;
    bool initialized;
} context_t;

static context_t s_ctx;

static esp_err_t lock_context(void)
{
    return xSemaphoreTake(s_ctx.lock, pdMS_TO_TICKS(1000)) == pdTRUE
               ? ESP_OK : ESP_ERR_TIMEOUT;
}

static void unlock_context(void)
{
    xSemaphoreGive(s_ctx.lock);
}

static esp_err_t nvs_initialize(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "erase NVS failed");
        err = nvs_flash_init();
    }
    return err;
}

/**
 * 从 NVS 中读取已保存的 Wi-Fi 账号和密码。
 *
 * @param ssid    输出 SSID 缓冲区
 * @param ssid_size SSID 缓冲区大小
 * @param password 输出密码缓冲区
 * @param password_size 密码缓冲区大小
 * @return ESP_OK 表示读取成功，ESP_ERR_NOT_FOUND 表示未保存
 */
static esp_err_t load_credentials(char *ssid, size_t ssid_size,char *password, size_t password_size)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);        //以只读方式打开NVS分区读取配置
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_ERR_NOT_FOUND;
    ESP_RETURN_ON_ERROR(err, TAG, "open NVS failed");
    size_t n = ssid_size;
    err = nvs_get_str(nvs, NVS_KEY_SSID, ssid, &n); 
    if (err == ESP_OK) {
        n = password_size;
        err = nvs_get_str(nvs, NVS_KEY_PASSWORD, password, &n);
    }
    nvs_close(nvs);
    return err == ESP_ERR_NVS_NOT_FOUND ? ESP_ERR_NOT_FOUND : err;
}

/**
 * 将 Wi-Fi 账号和密码保存到 NVS，确保配置持久化。
 *
 * @param ssid Wi-Fi SSID
 * @param password Wi-Fi 密码
 * @return 保存成功返回 ESP_OK
 */
static esp_err_t save_credentials(const char *ssid, const char *password)
{
    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs), TAG,
                        "open NVS failed");
    esp_err_t err = nvs_set_str(nvs, NVS_KEY_SSID, ssid);
    if (err == ESP_OK) err = nvs_set_str(nvs, NVS_KEY_PASSWORD, password);
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    return err;
}

/**
 * 对 HTTP 表单中的 URL 编码内容进行解码，例如 %20 和 + 转换为真实字符。
 */
static esp_err_t url_decode(char *destination, size_t capacity,
                            const char *source, size_t length)
{
    size_t output = 0;
    for (size_t i = 0; i < length; ++i) {
        ESP_RETURN_ON_FALSE(output + 1 < capacity, ESP_ERR_INVALID_SIZE, TAG,
                            "decoded form field too long");
        if (source[i] == '+') {
            destination[output++] = ' ';
        } else if (source[i] == '%' && i + 2 < length &&
                   isxdigit((unsigned char)source[i + 1]) &&
                   isxdigit((unsigned char)source[i + 2])) {
            char hex[3] = {source[i + 1], source[i + 2], 0};
            destination[output++] = (char)strtoul(hex, NULL, 16);
            i += 2;
        } else {
            destination[output++] = source[i];
        }
    }
    destination[output] = '\0';
    return ESP_OK;
}

/**
 * 从 HTTP POST 表单中提取某个字段的值，并进行解码处理。
 *
 * @param body    原始表单 body
 * @param key     需要获取的字段名
 * @param value   输出字段值缓冲区
 * @param capacity 缓冲区容量
 * @return 找到字段返回 ESP_OK，否则返回 ESP_ERR_NOT_FOUND
 */
static esp_err_t form_value(const char *body, const char *key,
                            char *value, size_t capacity)
{
    const size_t key_length = strlen(key);
    const char *field = body;
    while (field && *field) {
        const char *end = strchr(field, '&');
        const size_t length = end ? (size_t)(end - field) : strlen(field);
        if (length > key_length && field[key_length] == '=' &&
            memcmp(field, key, key_length) == 0) {
            return url_decode(value, capacity, field + key_length + 1,
                              length - key_length - 1);
        }
        field = end ? end + 1 : NULL;
    }
    return ESP_ERR_NOT_FOUND;
}

/**
 * 返回配网页面的 HTML 内容，供手机或浏览器访问。
 */
static esp_err_t root_get_handler(httpd_req_t *request)
{
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    return httpd_resp_send(request, s_page, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t start_softap(void);
static esp_err_t schedule_portal_start(void);

/**
 * 处理 Wi-Fi 配网表单提交，校验 SSID/密码并保存到 NVS 后尝试连接。
 */
static esp_err_t configure_post_handler(httpd_req_t *request)
{
    ESP_RETURN_ON_FALSE(request->content_len > 0 && request->content_len < POST_MAX_BYTES,
                        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,"Invalid form size"),
                        TAG, "invalid POST size");

    char body[POST_MAX_BYTES];
    size_t received = 0;
    while (received < request->content_len) {
        int n = httpd_req_recv(request, body + received, request->content_len - received);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
        ESP_RETURN_ON_FALSE(n > 0, ESP_FAIL, TAG, "receive POST failed");
        received += (size_t)n;
    }
    body[received] = '\0';

    char ssid[33] = {0};
    char password[65] = {0};
    esp_err_t err = form_value(body, "ssid", ssid, sizeof(ssid));
    if (err == ESP_OK) err = form_value(body, "password", password, sizeof(password));
    const size_t password_length = strlen(password);
    if (err != ESP_OK || ssid[0] == '\0' ||
        (password_length != 0 && (password_length < 8 || password_length > 63))) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "SSID required; password must be empty or >= 8 bytes");
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(save_credentials(ssid, password), TAG, "save credentials failed");

    wifi_config_t sta = {0};
    memcpy(sta.sta.ssid, ssid, strlen(ssid));
    memcpy(sta.sta.password, password, password_length);
    sta.sta.threshold.authmode = password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    sta.sta.pmf_cfg.capable = true;
    sta.sta.pmf_cfg.required = false;
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &sta), TAG, "set STA config failed");

    ESP_RETURN_ON_ERROR(lock_context(), TAG, "lock failed");
    strlcpy(s_ctx.status.sta_ssid, ssid, sizeof(s_ctx.status.sta_ssid));
    s_ctx.status.credentials_saved = true;
    s_ctx.status.state = WIFI_PROV_STATE_CONNECTING;
    s_ctx.retry_count = 0;
    unlock_context();

    httpd_resp_set_type(request, "text/html; charset=utf-8");
    ESP_RETURN_ON_ERROR(httpd_resp_sendstr(request, "<meta charset='utf-8'><h3>配置已保存，正在连接路由器……</h3>"),
        TAG, "send response failed");
    ESP_LOGI(TAG, "received Wi-Fi credentials for SSID '%s'", ssid);
    return esp_wifi_connect();
}

/**
 * 启动内嵌 HTTP 服务器。
 *
 * 只允许 portal_start_task() 在确认 SoftAP netif 已经 UP、AP IP 有效之后
 * 调用本函数。这样可以避免 AP 网络接口尚未就绪时 httpd_start() 内部
 * listen() 返回 EHOSTDOWN(112)。
 */
static esp_err_t start_http_server(void)
{
    esp_err_t err = lock_context();
    if (err != ESP_OK) {
        return err;
    }

    if (s_ctx.server != NULL) {
        unlock_context();
        return ESP_OK;
    }

    const bool provisioning =
        s_ctx.status.state == WIFI_PROV_STATE_PROVISIONING;
    unlock_context();

    if (!provisioning) {
        return ESP_ERR_INVALID_STATE;
    }

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 4;

    err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "start HTTP server failed: %s",
                 esp_err_to_name(err));
        return err;
    }

    const httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get_handler,
    };

    const httpd_uri_t configure = {
        .uri = "/configure",
        .method = HTTP_POST,
        .handler = configure_post_handler,
    };

    err = httpd_register_uri_handler(server, &root);
    if (err == ESP_OK) {
        err = httpd_register_uri_handler(server, &configure);
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register HTTP handler failed: %s",
                 esp_err_to_name(err));
        httpd_stop(server);
        return err;
    }

    /*
     * 创建 HTTP server 期间 STA 可能已经获得 IP，因此再次检查当前状态。
     * 如果 portal 已经不再需要，立即销毁刚创建的 server。
     */
    err = lock_context();
    if (err != ESP_OK) {
        httpd_stop(server);
        return err;
    }

    if (s_ctx.status.state == WIFI_PROV_STATE_PROVISIONING &&
        s_ctx.server == NULL) {
        s_ctx.server = server;
        server = NULL;
    }

    unlock_context();

    if (server != NULL) {
        httpd_stop(server);
    }

    return ESP_OK;
}

static void stop_http_server(void)
{
    httpd_handle_t server = NULL;

    if (lock_context() == ESP_OK) {
        server = s_ctx.server;
        s_ctx.server = NULL;
        unlock_context();
    }

    if (server != NULL) {
        esp_err_t err = httpd_stop(server);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "stop HTTP server failed: %s",
                     esp_err_to_name(err));
        }
    }
}

static bool portal_is_required(void)
{
    bool required = false;

    if (lock_context() == ESP_OK) {
        required = s_ctx.initialized &&
                   s_ctx.status.state == WIFI_PROV_STATE_PROVISIONING;
        unlock_context();
    }

    return required;
}

/**
 * 等待 SoftAP 网络接口真正可用，再启动 HTTP 配网页面。
 *
 * WIFI_EVENT_AP_START 只能说明 AP driver 已启动；这里进一步等待
 * esp_netif_is_netif_up() 为 true 且 AP 获得有效 IP，避免 httpd listen(112)。
 */
static void portal_start_task(void *arg)
{
    (void)arg;
    esp_err_t last_err = ESP_ERR_TIMEOUT;

    for (int attempt = 0;
         attempt < PORTAL_WAIT_MAX_ATTEMPTS;
         ++attempt) {

        if (!portal_is_required()) {
            last_err = ESP_ERR_INVALID_STATE;
            break;
        }

        esp_netif_t *ap_netif = s_ctx.ap_netif;

        if (ap_netif != NULL &&
            esp_netif_is_netif_up(ap_netif)) {

            esp_netif_ip_info_t ip_info = {0};
            esp_err_t ip_err =
                esp_netif_get_ip_info(ap_netif, &ip_info);

            if (ip_err == ESP_OK && ip_info.ip.addr != 0) {
                /*
                 * 给 AP/DHCP/lwIP 一个很短的稳定窗口。
                 * 该延时只发生在首次/回退配网，不影响正常 STA 运行。
                 */
                vTaskDelay(pdMS_TO_TICKS(100));

                if (!portal_is_required() ||
                    !esp_netif_is_netif_up(ap_netif)) {
                    last_err = ESP_ERR_INVALID_STATE;
                    break;
                }

                last_err = start_http_server();

                if (last_err == ESP_OK) {
                    ESP_LOGI(TAG,
                             "provisioning ready: connect '%s', "
                             "then open http://" IPSTR "/",
                             s_ctx.status.ap_ssid,
                             IP2STR(&ip_info.ip));
                    break;
                }

                if (last_err != ESP_ERR_INVALID_STATE) {
                    ESP_LOGW(TAG,
                             "portal start attempt %d failed: %s",
                             attempt + 1,
                             esp_err_to_name(last_err));
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(PORTAL_WAIT_INTERVAL_MS));
    }

    if (lock_context() == ESP_OK) {
        s_ctx.portal_start_pending = false;
        unlock_context();
    }

    if (last_err != ESP_OK &&
        last_err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG,
                 "provisioning portal failed after waiting "
                 "for AP netif: %s",
                 esp_err_to_name(last_err));
    }

    vTaskDelete(NULL);
}

/**
 * 串行化 portal 启动。
 *
 * AP_START、首次启动、STA 重试耗尽回退等路径都可能请求配网页面，
 * portal_start_pending 确保同一时刻只有一个启动任务。
 */
static esp_err_t schedule_portal_start(void)
{
    esp_err_t err = lock_context();
    if (err != ESP_OK) {
        return err;
    }

    if (s_ctx.server != NULL ||
        s_ctx.portal_start_pending ||
        s_ctx.status.state != WIFI_PROV_STATE_PROVISIONING) {
        unlock_context();
        return ESP_OK;
    }

    s_ctx.portal_start_pending = true;
    unlock_context();

    if (xTaskCreate(portal_start_task,
                    "wifi_prov_http",
                    PORTAL_TASK_STACK_SIZE,
                    NULL,
                    PORTAL_TASK_PRIORITY,
                    NULL) != pdPASS) {

        if (lock_context() == ESP_OK) {
            s_ctx.portal_start_pending = false;
            unlock_context();
        }

        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

/**
 * 进入 SoftAP 配网模式。
 *
 * 注意：本函数不直接启动 HTTP server。
 * HTTP 统一由 portal_start_task() 等待 AP netif ready 后启动。
 */
static esp_err_t start_softap(void)
{
    wifi_mode_t mode;

    ESP_RETURN_ON_ERROR(esp_wifi_get_mode(&mode),
                        TAG, "get mode failed");

    if (mode != WIFI_MODE_APSTA) {
        ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_APSTA),
                            TAG, "set APSTA mode failed");
    }

    ESP_RETURN_ON_ERROR(lock_context(), TAG, "lock failed");
    s_ctx.status.state = WIFI_PROV_STATE_PROVISIONING;
    unlock_context();

    return schedule_portal_start();
}

static void event_handler(void *arg, esp_event_base_t base,
                          int32_t event_id, void *event_data)
{
    (void)arg;

    if (base == WIFI_EVENT &&
        event_id == WIFI_EVENT_STA_START) {

        /*
         * STA_START 只负责已有凭据时的 STA 连接。
         * 不能在这里启动 HTTP portal，因为 AP netif 此时可能尚未 UP。
         */
        bool credentials_saved = false;

        if (lock_context() == ESP_OK) {
            credentials_saved = s_ctx.status.credentials_saved;
            unlock_context();
        }

        if (credentials_saved) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_connect());
        }

    } else if (base == WIFI_EVENT &&
               event_id == WIFI_EVENT_AP_START) {

        /*
         * AP_START 只负责调度。真正 httpd_start() 前，
         * portal task 还会等待 AP netif UP + IP 有效。
         */
        if (portal_is_required()) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(
                schedule_portal_start());
        }

    } else if (base == WIFI_EVENT &&
               event_id == WIFI_EVENT_STA_DISCONNECTED) {

        bool retry = false;
        uint8_t retry_count = 0;
        uint8_t retry_max = 0;

        if (lock_context() == ESP_OK) {
            retry_max = s_ctx.config.sta_max_retry;

            if (s_ctx.status.credentials_saved &&
                s_ctx.retry_count < s_ctx.config.sta_max_retry) {

                s_ctx.retry_count++;
                retry_count = s_ctx.retry_count;
                s_ctx.status.state = WIFI_PROV_STATE_CONNECTING;
                retry = true;

            } else {
                s_ctx.status.state = WIFI_PROV_STATE_PROVISIONING;
            }

            unlock_context();
        }

        if (retry) {
            ESP_LOGW(TAG, "STA disconnected, retry %u/%u",
                     (unsigned)retry_count,
                     (unsigned)retry_max);
            ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_connect());
        } else {
            ESP_LOGW(TAG,
                     "STA connection unavailable, "
                     "enter provisioning mode");
            ESP_ERROR_CHECK_WITHOUT_ABORT(start_softap());
        }

    } else if (base == IP_EVENT &&
               event_id == IP_EVENT_STA_GOT_IP) {

        const ip_event_got_ip_t *event =
            (const ip_event_got_ip_t *)event_data;

        if (lock_context() == ESP_OK) {
            s_ctx.retry_count = 0;
            s_ctx.status.ip = event->ip_info.ip;
            s_ctx.status.state = WIFI_PROV_STATE_CONNECTED;
            unlock_context();
        }

        ESP_LOGI(TAG, "STA connected, IP=" IPSTR,
                 IP2STR(&event->ip_info.ip));

        /*
         * 先把状态设置为 CONNECTED，再停止 portal。
         * 尚未执行完成的 portal task 会看到状态变化并自行退出。
         */
        stop_http_server();

        if (s_ctx.config.stop_ap_after_connected) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(
                esp_wifi_set_mode(WIFI_MODE_STA));
        }
    }
}

/**
 * 初始化 Wi-Fi 配网流程，创建 AP/STA 网络接口并加载历史凭据。
 *
 * 若已保存过 Wi-Fi 信息，则直接启动 STA 连接；否则进入配网模式并开启 HTTP 页面。
 */
esp_err_t wifi_provisioning_start(const wifi_provisioning_config_t *config)
{
    ESP_RETURN_ON_FALSE(!s_ctx.initialized, ESP_ERR_INVALID_STATE, TAG,
                        "already started");
    //默认的配置
    wifi_provisioning_config_t selected = {
        .ap_ssid_prefix = WIFI_SID, .ap_password = WIFI_PASSWORD,
        .sta_max_retry = 5, .stop_ap_after_connected = true,
    };
    if (config) selected = *config;
    ESP_RETURN_ON_FALSE(selected.ap_ssid_prefix && selected.sta_max_retry > 0 &&
                            (!selected.ap_password || !selected.ap_password[0] ||
                             (strlen(selected.ap_password) >= 8 &&
                              strlen(selected.ap_password) <= 63)),
                        ESP_ERR_INVALID_ARG, TAG, "invalid configuration");


    ESP_RETURN_ON_ERROR(nvs_initialize(), TAG, "initialize NVS failed");
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "initialize netif failed");
    esp_err_t err = esp_event_loop_create_default();
    ESP_RETURN_ON_FALSE(err == ESP_OK || err == ESP_ERR_INVALID_STATE, err, TAG,
                        "create event loop failed");
    s_ctx.lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_ctx.lock, ESP_ERR_NO_MEM, TAG, "create mutex failed");
    s_ctx.config = selected;
    s_ctx.sta_netif = esp_netif_create_default_wifi_sta();
    s_ctx.ap_netif = esp_netif_create_default_wifi_ap();
    ESP_RETURN_ON_FALSE(s_ctx.sta_netif && s_ctx.ap_netif, ESP_ERR_NO_MEM, TAG,
                        "create network interfaces failed");

    /*
     * Low-memory Wi-Fi profile for ESP32-S3 + camera + ESP-DL coexistence.
     *
     * Do not rely only on sdkconfig here. This project can be rebuilt from an
     * older sdkconfig where WIFI_INIT_CONFIG_DEFAULT() still expands to
     * static_rx=10, dynamic_rx=32, dynamic_tx=32. Those defaults require too
     * much internal/DMA-capable RAM after the face model has started.
     *
     * WIFI_INIT_CONFIG_DEFAULT() is still used first so all IDF-version fields
     * and magic stay valid, then only the memory-sensitive fields are
     * overridden.
     */
    wifi_init_config_t initialization = WIFI_INIT_CONFIG_DEFAULT();

    initialization.static_rx_buf_num = 6;
    initialization.dynamic_rx_buf_num = 16;

    /* 1 == dynamic TX buffer mode in ESP-IDF 5.x */
    initialization.tx_buf_type = 1;
    initialization.static_tx_buf_num = 0;
    initialization.dynamic_tx_buf_num = 16;

    /*
     * Reduce management-buffer pressure as well. 16 short management buffers
     * is ample for a small SoftAP + one STA provisioning workload.
     */
    initialization.rx_mgmt_buf_num = 4;
    initialization.mgmt_sbuf_num = 16;

    /* Keep AMPDU RX window no larger than the static RX buffer count. */
    initialization.rx_ba_win = 6;

    ESP_LOGI(TAG,
             "Wi-Fi init profile: static_rx=%d dynamic_rx=%d "
             "dynamic_tx=%d rx_mgmt=%d mgmt_short=%d ba_win=%d",
             initialization.static_rx_buf_num,
             initialization.dynamic_rx_buf_num,
             initialization.dynamic_tx_buf_num,
             initialization.rx_mgmt_buf_num,
             initialization.mgmt_sbuf_num,
             initialization.rx_ba_win);

    ESP_RETURN_ON_ERROR(esp_wifi_init(&initialization), TAG, "init Wi-Fi failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(
                            WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL,
                            &s_ctx.wifi_handler), TAG, "register Wi-Fi event failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(
                            IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, NULL,
                            &s_ctx.ip_handler), TAG, "register IP event failed");

    //使用MAC地址作为热点名的后缀(唯一性)
    uint8_t mac[6];
    ESP_RETURN_ON_ERROR(esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP), TAG,
                        "read MAC failed");
    snprintf(s_ctx.status.ap_ssid, sizeof(s_ctx.status.ap_ssid), "%s-%02X%02X%02X",
             selected.ap_ssid_prefix, mac[3], mac[4], mac[5]);

    //AP热点的配置以及启动
    wifi_config_t ap = {0};
    strlcpy((char *)ap.ap.ssid, s_ctx.status.ap_ssid, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = strlen(s_ctx.status.ap_ssid);
    ap.ap.channel = 1;
    ap.ap.max_connection = 4;
    ap.ap.authmode = selected.ap_password && selected.ap_password[0]
                         ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    if (selected.ap_password) {
        strlcpy((char *)ap.ap.password, selected.ap_password,
                sizeof(ap.ap.password));
    }
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_APSTA), TAG,
                        "set initial mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap), TAG,
                        "set SoftAP config failed");

    //读取原有的数据，使用STA尝试连接wifi
    char ssid[33] = {0};
    char password[65] = {0};
    err = load_credentials(ssid, sizeof(ssid), password, sizeof(password));
    if (err == ESP_OK) {
        wifi_config_t sta = {0};
        memcpy(sta.sta.ssid, ssid, strlen(ssid));
        memcpy(sta.sta.password, password, strlen(password));
        sta.sta.threshold.authmode = password[0] ? WIFI_AUTH_WPA2_PSK
                                                 : WIFI_AUTH_OPEN;
        sta.sta.pmf_cfg.capable = true;
        ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &sta), TAG,
                            "set saved STA config failed");
        strlcpy(s_ctx.status.sta_ssid, ssid, sizeof(s_ctx.status.sta_ssid));
        s_ctx.status.credentials_saved = true;
        s_ctx.status.state = WIFI_PROV_STATE_CONNECTING;
    } else {
        ESP_RETURN_ON_FALSE(err == ESP_ERR_NOT_FOUND, err, TAG,
                            "load credentials failed");
        s_ctx.status.state = WIFI_PROV_STATE_PROVISIONING;
    }
    s_ctx.initialized = true;
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start Wi-Fi failed");

    /*
     * 不能在 esp_wifi_start() 返回后立即 httpd_start()：
     * SoftAP 对应的 lwIP/netif 可能尚未完全 ready。
     * portal task 会等待 AP netif UP 后再启动 HTTP。
     */
    if (!s_ctx.status.credentials_saved) {
        ESP_RETURN_ON_ERROR(schedule_portal_start(), TAG,
                            "schedule portal failed");
    }

    return ESP_OK;
}

/**
 * 获取当前 Wi-Fi 配网状态，包括连接状态、IP 和已保存的 SSID。
 */
esp_err_t wifi_provisioning_get_status(wifi_provisioning_status_t *status)
{
    ESP_RETURN_ON_FALSE(status && s_ctx.initialized, ESP_ERR_INVALID_STATE, TAG,
                        "not started or output is NULL");
    ESP_RETURN_ON_ERROR(lock_context(), TAG, "lock failed");
    *status = s_ctx.status;
    unlock_context();
    return ESP_OK;
}

/**
 * 清空已保存的 Wi-Fi 凭据并重新进入配网模式，供用户重新设置网络。
 */
esp_err_t wifi_provisioning_reset(void)
{
    ESP_RETURN_ON_FALSE(s_ctx.initialized,
                        ESP_ERR_INVALID_STATE,
                        TAG,
                        "not started");

    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(
        nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs),
        TAG, "open NVS failed");

    esp_err_t err = nvs_erase_all(nvs);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);

    ESP_RETURN_ON_ERROR(err, TAG, "erase credentials failed");

    /*
     * 先更新内部状态，再断开 STA。
     * esp_wifi_disconnect() 会触发 STA_DISCONNECTED 事件，
     * 因此事件处理器必须先看到 credentials_saved=false。
     */
    ESP_RETURN_ON_ERROR(lock_context(), TAG, "lock failed");

    memset(s_ctx.status.sta_ssid,
           0,
           sizeof(s_ctx.status.sta_ssid));

    s_ctx.status.credentials_saved = false;
    s_ctx.status.state = WIFI_PROV_STATE_PROVISIONING;
    s_ctx.retry_count = 0;

    unlock_context();

    /*
     * 当前可能本来就没有连接。disconnect 失败不应阻止进入配网模式。
     */
    err = esp_wifi_disconnect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "disconnect before provisioning: %s",
                 esp_err_to_name(err));
    }

    return start_softap();
}
