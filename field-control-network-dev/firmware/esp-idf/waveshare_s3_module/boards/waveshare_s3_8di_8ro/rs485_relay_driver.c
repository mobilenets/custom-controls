#include "rs485_relay_driver.h"
#include "board_profile.h"

#include "driver/gpio.h"
#include "driver/uart.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <stddef.h>
#include <stdint.h>


static const char *TAG = "rs485_relay";

#define RS485_UART_PORT        UART_NUM_1
#define RS485_SLAVE_ID        1
#define RS485_TIMEOUT_MS      200
#define RS485_TURN_US         200


static bool initialized = false;


/*
 * Standard Modbus RTU CRC-16.
 */
static uint16_t modbus_crc16(
    const uint8_t *data,
    size_t len
)
{
    uint16_t crc = 0xFFFF;

    for (size_t i = 0; i < len; i++) {

        crc ^= data[i];

        for (int bit = 0; bit < 8; bit++) {

            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }

    return crc;
}


/*
 * Control the RS485 transceiver direction.
 *
 * true  = transmit
 * false = receive
 */
static void rs485_set_tx(bool tx)
{
    gpio_set_level(
        FCN_RS485_DE_RE_GPIO,
        tx ? 1 : 0
    );

    esp_rom_delay_us(RS485_TURN_US);
}


/*
 * Read exactly the requested number of bytes or time out.
 */
static int rs485_read_response(
    uint8_t *buffer,
    size_t length
)
{
    size_t received = 0;

    int64_t deadline =
        esp_timer_get_time() +
        ((int64_t)RS485_TIMEOUT_MS * 1000);

    while (
        received < length &&
        esp_timer_get_time() < deadline
    ) {

        int count = uart_read_bytes(
            RS485_UART_PORT,
            buffer + received,
            length - received,
            pdMS_TO_TICKS(10)
        );

        if (count > 0) {
            received += (size_t)count;
        }
    }

    return (int)received;
}


/*
 * Read eight Modbus coils.
 *
 * Request:
 * [id][01][addr hi][addr lo][00][08][crc lo][crc hi]
 *
 * Expected response:
 * [id][01][01][coil byte][crc lo][crc hi]
 */
static bool modbus_read_coils_8(
    uint8_t slave_id,
    uint16_t start_addr,
    uint8_t *coil_mask
)
{
    if (!initialized || coil_mask == NULL) {
        return false;
    }


    uint8_t request[8];

    request[0] = slave_id;
    request[1] = 0x01;
    request[2] = (uint8_t)(start_addr >> 8);
    request[3] = (uint8_t)(start_addr & 0xFF);
    request[4] = 0x00;
    request[5] = 0x08;


    uint16_t crc = modbus_crc16(
        request,
        6
    );

    request[6] = (uint8_t)(crc & 0xFF);
    request[7] = (uint8_t)(crc >> 8);


    /*
     * Remove any stale bytes before beginning a transaction.
     */
    uart_flush_input(RS485_UART_PORT);


    rs485_set_tx(true);


    int written = uart_write_bytes(
        RS485_UART_PORT,
        request,
        sizeof(request)
    );

    if (written != sizeof(request)) {

        rs485_set_tx(false);

        ESP_LOGW(
            TAG,
            "Failed to transmit complete Modbus request"
        );

        return false;
    }


    /*
     * Wait until the UART has physically transmitted the frame
     * before switching the transceiver back to receive mode.
     */
    esp_err_t err = uart_wait_tx_done(
        RS485_UART_PORT,
        pdMS_TO_TICKS(100)
    );


    rs485_set_tx(false);


    if (err != ESP_OK) {

        ESP_LOGW(
            TAG,
            "UART transmit timeout"
        );

        return false;
    }


    uint8_t response[6] = {0};

    int received = rs485_read_response(
        response,
        sizeof(response)
    );


    if (received != sizeof(response)) {

        ESP_LOGD(
            TAG,
            "Modbus response timeout (%d/6 bytes)",
            received
        );

        return false;
    }


    uint16_t received_crc =
        (uint16_t)response[4] |
        ((uint16_t)response[5] << 8);


    uint16_t calculated_crc =
        modbus_crc16(
            response,
            4
        );


    if (received_crc != calculated_crc) {

        ESP_LOGW(
            TAG,
            "Modbus CRC mismatch"
        );

        return false;
    }


    if (
        response[0] != slave_id ||
        response[1] != 0x01 ||
        response[2] != 0x01
    ) {

        ESP_LOGW(
            TAG,
            "Unexpected Modbus response"
        );

        return false;
    }


    *coil_mask = response[3];

    return true;
}


esp_err_t rs485_relay_init(void)
{
    ESP_LOGI(
        TAG,
        "Initializing RS485 relay interface"
    );

    ESP_LOGI(
        TAG,
        "UART TX=%d RX=%d DE/RE=%d baud=%d",
        FCN_RS485_TX_GPIO,
        FCN_RS485_RX_GPIO,
        FCN_RS485_DE_RE_GPIO,
        FCN_RS485_BAUD
    );


    gpio_config_t direction_config = {
        .pin_bit_mask =
            (1ULL << FCN_RS485_DE_RE_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };


    esp_err_t err = gpio_config(
        &direction_config
    );

    if (err != ESP_OK) {
        return err;
    }


    /*
     * Default to receive mode before enabling the UART.
     */
    gpio_set_level(
        FCN_RS485_DE_RE_GPIO,
        0
    );


    uart_config_t uart_config = {
        .baud_rate = FCN_RS485_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT
    };


    err = uart_param_config(
        RS485_UART_PORT,
        &uart_config
    );

    if (err != ESP_OK) {
        return err;
    }


    err = uart_set_pin(
        RS485_UART_PORT,
        FCN_RS485_TX_GPIO,
        FCN_RS485_RX_GPIO,
        UART_PIN_NO_CHANGE,
        UART_PIN_NO_CHANGE
    );

    if (err != ESP_OK) {
        return err;
    }


    err = uart_driver_install(
        RS485_UART_PORT,
        256,
        256,
        0,
        NULL,
        0
    );

    if (err != ESP_OK) {
        return err;
    }


    initialized = true;

    ESP_LOGI(
        TAG,
        "RS485 interface initialized"
    );

    return ESP_OK;
}


bool rs485_relay_detect(uint8_t *coil_mask)
{
    uint8_t coils = 0;


    if (!modbus_read_coils_8(
            RS485_SLAVE_ID,
            0x0000,
            &coils
        )) {

        ESP_LOGI(
            TAG,
            "RS485 expansion NOT detected"
        );

        return false;
    }


    if (coil_mask != NULL) {
        *coil_mask = coils;
    }


    ESP_LOGI(
        TAG,
        "RS485 expansion detected, coil mask=0x%02X",
        coils
    );

    return true;
}
