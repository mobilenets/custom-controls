#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t fcn_input_init(void);

/*
 * Read one logical input.
 *
 * input_number is 1-based.
 * Returns:
 *   0 = LOW
 *   1 = HIGH
 *  -1 = invalid input number
 */
int fcn_input_get(uint8_t input_number);

/*
 * Read all onboard digital inputs.
 *
 * bit 0 = input 1
 * bit 7 = input 8
 */
uint8_t fcn_input_get_mask(void);

esp_err_t fcn_input_start_monitor(void);

#ifdef __cplusplus
}
#endif
