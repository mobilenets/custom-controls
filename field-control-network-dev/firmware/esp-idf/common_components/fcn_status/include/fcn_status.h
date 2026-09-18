#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    FCN_STATUS_OFF = 0,
    FCN_STATUS_AGENT_CONNECTED

} fcn_status_t;

esp_err_t fcn_status_init(void);
esp_err_t fcn_status_set(fcn_status_t status);

#ifdef __cplusplus
}
#endif
