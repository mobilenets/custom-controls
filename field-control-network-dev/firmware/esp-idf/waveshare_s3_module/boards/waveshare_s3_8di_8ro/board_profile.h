#pragma once

/*
 * FCN Board Profile
 *
 * Waveshare ESP32-S3-ETH-8DI-8RO
 *
 * IMPORTANT:
 * Application-level FCN code should not depend on ESP32-S3-specific
 * details. Those details belong in this board profile and its drivers.
 */

#define FCN_BOARD_NAME             "Waveshare ESP32-S3-ETH-8DI-8RO"
#define FCN_BOARD_ID               "waveshare_s3_8di_8ro"

/* Capabilities */
#define FCN_HAS_WIFI               1
#define FCN_HAS_ETHERNET           1
#define FCN_HAS_RELAYS             1
#define FCN_HAS_DIGITAL_INPUTS     1
#define FCN_HAS_RS485              1
#define FCN_HAS_CAMERA             0

#define FCN_ONBOARD_RELAY_COUNT    8
#define FCN_DIGITAL_INPUT_COUNT    8
#define FCN_RS485_RELAY_COUNT      8

/*
 * Onboard relay expander
 */
#define FCN_I2C_SDA_GPIO           42
#define FCN_I2C_SCL_GPIO           41

#define FCN_RELAY_EXPANDER_ADDR    0x20
#define FCN_RELAY_REG_OUTPUT       0x01
#define FCN_RELAY_REG_POLARITY     0x02
#define FCN_RELAY_REG_CONFIG       0x03

/*
 * Digital inputs DI1-DI8
 */
#define FCN_DI1_GPIO               4
#define FCN_DI2_GPIO               5
#define FCN_DI3_GPIO               6
#define FCN_DI4_GPIO               7
#define FCN_DI5_GPIO               8
#define FCN_DI6_GPIO               9
#define FCN_DI7_GPIO               10
#define FCN_DI8_GPIO               11

/*
 * W5500 Ethernet
 */
#define FCN_ETH_CS_GPIO            16
#define FCN_ETH_IRQ_GPIO           12
#define FCN_ETH_MOSI_GPIO          13
#define FCN_ETH_MISO_GPIO          14
#define FCN_ETH_SCLK_GPIO          15
#define FCN_ETH_PHY_ADDR           1

/*
 * RS485 expansion
 */
#define FCN_RS485_TX_GPIO          17
#define FCN_RS485_RX_GPIO          18
#define FCN_RS485_DE_RE_GPIO       2
#define FCN_RS485_BAUD             9600

/*
 * Onboard status LED
 */
#define FCN_STATUS_LED_GPIO        38
