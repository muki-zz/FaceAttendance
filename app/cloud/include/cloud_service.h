#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool wifi_connected;
    bool mqtt_connected;
    bool time_synced;
    bool cloud_enabled;
    uint32_t last_uploaded_id;
    uint32_t pending_batch_count;
    esp_err_t last_error;
} cloud_service_status_t;

/**
 * Start the independent cloud/time service task.
 * Wi-Fi is started only when cloud sync is enabled in attendance_config.json,
 * and is intentionally delayed so camera startup is not disturbed.
 */
esp_err_t cloud_service_start(void);

/** Snapshot of connectivity/time/upload state for diagnostics or a future GUI status widget. */
esp_err_t cloud_service_get_status(cloud_service_status_t *out);

#ifdef __cplusplus
}
#endif
