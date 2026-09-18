#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t rs485_relay_init(void);
bool rs485_relay_detect(uint8_t *coil_mask);
