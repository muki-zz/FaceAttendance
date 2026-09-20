#pragma once
#include "pca9557.h"
// Borrow the board's shared expander; never create/delete it here.
esp_err_t board_pa_prepare(pca9557_handle_t expander);
esp_err_t board_pa_set(pca9557_handle_t expander, bool on);
