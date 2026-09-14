#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t fcn_network_init(void);

bool fcn_network_link_up(void);
bool fcn_network_has_ip(void);

#ifdef __cplusplus
}
#endif
