#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_camera.h"
#include "wifi.h"
/*
 * AI-Thinker ESP32-CAM pin assignment
 */

#define CAM_PIN_PWDN   32
#define CAM_PIN_RESET  -1

#define CAM_PIN_XCLK    0
#define CAM_PIN_SIOD   26
#define CAM_PIN_SIOC   27

#define CAM_PIN_D7     35
#define CAM_PIN_D6     34
#define CAM_PIN_D5     39
#define CAM_PIN_D4     36
#define CAM_PIN_D3     21
#define CAM_PIN_D2     19
#define CAM_PIN_D1     18
#define CAM_PIN_D0      5

#define CAM_PIN_VSYNC  25
#define CAM_PIN_HREF   23
#define CAM_PIN_PCLK   22


static esp_err_t camera_init(void)
{
    camera_config_t config = {
        .pin_pwdn       = CAM_PIN_PWDN,
        .pin_reset      = CAM_PIN_RESET,
        .pin_xclk       = CAM_PIN_XCLK,

        .pin_sccb_sda   = CAM_PIN_SIOD,
        .pin_sccb_scl   = CAM_PIN_SIOC,

        .pin_d7         = CAM_PIN_D7,
        .pin_d6         = CAM_PIN_D6,
        .pin_d5         = CAM_PIN_D5,
        .pin_d4         = CAM_PIN_D4,
        .pin_d3         = CAM_PIN_D3,
        .pin_d2         = CAM_PIN_D2,
        .pin_d1         = CAM_PIN_D1,
        .pin_d0         = CAM_PIN_D0,

        .pin_vsync      = CAM_PIN_VSYNC,
        .pin_href       = CAM_PIN_HREF,
        .pin_pclk       = CAM_PIN_PCLK,

        .xclk_freq_hz   = 20000000,

        .ledc_timer     = LEDC_TIMER_0,
        .ledc_channel   = LEDC_CHANNEL_0,

        .pixel_format   = PIXFORMAT_JPEG,

        /*
         * Keep the first test modest.
         * VGA = 640 x 480
         */
        .frame_size     = FRAMESIZE_VGA,

        /*
         * Lower number = higher JPEG quality.
         */
        .jpeg_quality   = 12,

        /*
         * One framebuffer for this first test.
         */
        .fb_count       = 1,

        /*
         * Put the framebuffer in PSRAM.
         */
        .fb_location    = CAMERA_FB_IN_PSRAM,

        .grab_mode      = CAMERA_GRAB_WHEN_EMPTY
    };

    return esp_camera_init(&config);
}


void app_main(void)
{
    printf("\n");
    printf("===============================\n");
    printf(" Field Control Network\n");
    printf(" ESP32-CAM Diagnostic Node\n");
    printf(" Native ESP-IDF\n");
    printf("===============================\n");

    size_t psram_total =
        heap_caps_get_total_size(MALLOC_CAP_SPIRAM);

    size_t psram_free =
        heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    printf("\nMemory diagnostics:\n");
    printf("PSRAM total: %u bytes\n",
           (unsigned)psram_total);
    printf("PSRAM free : %u bytes\n",
           (unsigned)psram_free);

    if (psram_total > 0) {
        printf("PSRAM status: DETECTED\n");
    } else {
        printf("PSRAM status: NOT DETECTED\n");
    }

    printf("\nInitializing camera...\n");

    esp_err_t err = camera_init();

    if (err != ESP_OK) {
        printf("Camera init: FAILED\n");
        printf("Error code: 0x%x\n", err);

        while (1) {
            printf("FCN camera fault\n");
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }

    printf("Camera init: OK\n");

    /*
     * Capture one frame.
     */
    printf("Capturing test frame...\n");

    camera_fb_t *fb = esp_camera_fb_get();

    if (fb == NULL) {
        printf("Frame capture: FAILED\n");
    } else {
        printf("Frame capture: OK\n");
        printf("Width : %u\n", fb->width);
        printf("Height: %u\n", fb->height);
        printf("JPEG size: %u bytes\n",
               (unsigned)fb->len);

        esp_camera_fb_return(fb);
    }

    printf("\nESP32-CAM-003 ready.\n\n");

    int count = 0;

    while (1) {
        printf("FCN alive: %d\n", count++);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
