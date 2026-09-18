#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Initialize the relay subsystem.
 *
 * Board-specific code decides how the physical relays are implemented.
 */
esp_err_t fcn_relay_init(void);

/*
 * Set one relay.
 *
 * relay_number is 1-based.
 */
esp_err_t fcn_relay_set(uint8_t relay_number, bool on);

int fcn_relay_get(uint8_t relay_number);

esp_err_t fcn_relay_refresh(void);

/*
 * Set all onboard relays OFF.
 */
esp_err_t fcn_relay_all_off(void);

/*
 * Read the current logical onboard relay mask.
 *
 * Bit 0 = relay 1
 * Bit 7 = relay 8
 */
uint8_t fcn_relay_get_mask(void);

#ifdef __cplusplus
}
#endif
