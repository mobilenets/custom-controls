#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Show a serial configuration window.
 *
 * Returns true if the user requested configuration mode.
 */
bool fcn_config_boot_window(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif