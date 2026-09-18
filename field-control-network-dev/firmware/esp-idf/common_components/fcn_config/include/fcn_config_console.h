#pragma once

#include "esp_err.h"
#include "fcn_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Run the blocking serial configuration console.
 *
 * loaded_config is the currently active configuration.
 * The console edits a working copy.
 *
 * S = save to NVS, reload, and return ESP_OK
 * C = cancel all unsaved edits and return ESP_ERR_INVALID_STATE
 */
esp_err_t fcn_config_console_run(fcn_config_t *loaded_config);

#ifdef __cplusplus
}
#endif