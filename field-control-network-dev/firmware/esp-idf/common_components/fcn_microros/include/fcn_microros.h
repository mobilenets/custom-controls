#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    const char *agent_ip;
    uint16_t agent_port;

    uint8_t module_id;
    const char *module_name;

    uint8_t input_count;
    uint8_t output_count;


} fcn_microros_config_t;

esp_err_t fcn_microros_start(
    const fcn_microros_config_t *config
);

bool fcn_microros_agent_connected(void);

#ifdef __cplusplus
}
#endif
