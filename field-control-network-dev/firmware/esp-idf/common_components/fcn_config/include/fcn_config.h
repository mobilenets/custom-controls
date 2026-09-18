#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FCN_WIFI_SSID_MAX_LEN        32
#define FCN_WIFI_PASSWORD_MAX_LEN    64
#define FCN_AGENT_IP_MAX_LEN         15
#define FCN_MODULE_NAME_MAX_LEN      31

typedef struct
{
    char wifi_ssid[FCN_WIFI_SSID_MAX_LEN + 1];
    char wifi_password[FCN_WIFI_PASSWORD_MAX_LEN + 1];

    char agent_ip[FCN_AGENT_IP_MAX_LEN + 1];
    uint16_t agent_port;

    uint8_t module_id;
    char module_name[FCN_MODULE_NAME_MAX_LEN + 1];

    uint8_t input_count;
    uint8_t output_count;

    bool ethernet_enabled;
    bool wifi_fallback_enabled;
} fcn_config_t;

/* Fill a config object with board/project defaults. */
void fcn_config_set_defaults(fcn_config_t *config);

/* Initialize NVS and load config. Defaults are used for missing values. */
esp_err_t fcn_config_load(fcn_config_t *config);

/* Validate and persist the complete config atomically enough for FCN use. */
esp_err_t fcn_config_save(const fcn_config_t *config);

/* Reload the persisted values into config. */
esp_err_t fcn_config_reload(fcn_config_t *config);

/* Validate a complete configuration object. */
bool fcn_config_is_valid(const fcn_config_t *config);

/* Compare two configs. */
bool fcn_config_equal(const fcn_config_t *a, const fcn_config_t *b);

/* Wi-Fi credentials are optional unless Wi-Fi fallback is enabled. */
bool fcn_config_wifi_available(const fcn_config_t *config);

#ifdef __cplusplus
}
#endif