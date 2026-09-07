#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"

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
    printf("PSRAM total: %u bytes\n", (unsigned)psram_total);
    printf("PSRAM free : %u bytes\n", (unsigned)psram_free);

    if (psram_total > 0) {
        printf("PSRAM status: DETECTED\n");
    } else {
        printf("PSRAM status: NOT DETECTED\n");
    }

    printf("\n");

    int count = 0;

    while (1) {
        printf("FCN alive: %d\n", count++);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
