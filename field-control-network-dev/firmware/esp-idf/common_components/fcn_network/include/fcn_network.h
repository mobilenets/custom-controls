#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    bool ethernet_enabled;
    bool wifi_fallback_enabled;

    const char *wifi_ssid;
    const char *wifi_password;

} fcn_network_config_t;

esp_err_t fcn_network_init(
    const fcn_network_config_t *config
);

bool fcn_network_link_up(void);
bool fcn_network_has_ip(void);

const char *fcn_network_get_ip_string(void);
const char *fcn_network_get_gateway_string(void);
const char *fcn_network_get_netmask_string(void);

#ifdef __cplusplus
}
#endif