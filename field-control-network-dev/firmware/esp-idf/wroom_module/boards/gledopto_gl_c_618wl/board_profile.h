#pragma once

/*
 * Field Control Network
 * Board Profile
 *
 * GLEDOPTO Elite 4D-EXMU
 * Model: GL-C-618WL
 *
 * ESP32-D0WD-V3
 * 4 MB flash
 */

#define FCN_BOARD_NAME             "GLEDOPTO Elite 4D-EXMU"
#define FCN_BOARD_ID               "gledopto_gl_c_618wl"

#define FCN_HAS_WIFI               1
#define FCN_HAS_ETHERNET           1

/*
 * External I/O
 *
 * Four current-amplified output channels.
 * GPIO assignments will be verified individually
 * on the physical hardware.
 */

#define FCN_OUTPUT_COUNT           4
#define FCN_OUTPUT1_GPIO           16
#define FCN_OUTPUT2_GPIO           12
#define FCN_OUTPUT3_GPIO           4
#define FCN_OUTPUT4_GPIO           2

#define FCN_OUTPUT_ACTIVE_LEVEL    1
#define FCN_OUTPUT_INACTIVE_LEVEL  0
/*
 * Additional bidirectional GPIO.
 *
 * Initially treated as an input by FCN.
 */
#define FCN_DI1_GPIO               13
#define FCN_DIGITAL_INPUT_COUNT    1