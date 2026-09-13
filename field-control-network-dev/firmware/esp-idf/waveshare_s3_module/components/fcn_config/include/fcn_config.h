#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FCN_WIFI_SSID_MAX_LEN       32
#define FCN_WIFI_PASSWORD_MAX_LEN   64
#define FCN_AGENT_IP_MAX_LEN        15

typedef struct
{
    char wifi_ssid[FCN_WIFI_SSID_MAX_LEN + 1];
    char wifi_password[FCN_WIFI_PASSWORD_MAX_LEN + 1];

    char agent_ip[FCN_AGENT_IP_MAX_LEN + 1];
    uint16_t agent_port;

    uint8_t module_id;

} fcn_config_t;


/*
 * Initialize a configuration structure to safe defaults.
 *
 * NVS loading and the serial 'press c' configuration protocol
 * will be added in a following milestone.
 */
void fcn_config_init(fcn_config_t *config);


/*
 * Returns true when the minimum configuration required for
 * FCN operation is present.
 */
bool fcn_config_is_valid(const fcn_config_t *config);


#ifdef __cplusplus
}
#endif
