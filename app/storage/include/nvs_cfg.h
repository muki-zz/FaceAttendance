#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t nvs_cfg_init(void);
esp_err_t nvs_cfg_get_u32(const char *key, uint32_t default_value, uint32_t *value_out);
esp_err_t nvs_cfg_set_u32(const char *key, uint32_t value);

#ifdef __cplusplus
}
#endif
