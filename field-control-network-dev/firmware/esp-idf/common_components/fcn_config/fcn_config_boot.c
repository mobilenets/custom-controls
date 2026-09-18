#include "fcn_config_boot.h"

#include <stdio.h>
#include <ctype.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

static const char *TAG = "fcn_config_boot";

bool fcn_config_boot_window(uint32_t timeout_ms)
{
    const uint32_t poll_ms = 50;
    uint32_t elapsed_ms = 0;

    printf("\n");
    printf("=======================================\n");
    printf(" FCN configuration window\n");
    printf(" Press 'c' then ENTER within %lu seconds\n",
           (unsigned long)(timeout_ms / 1000));
    printf("=======================================\n");
    fflush(stdout);

    while (elapsed_ms < timeout_ms)
    {
        int ch = getchar();

        if (ch != EOF)
        {
            ch = tolower(ch);

            if (ch == 'c')
            {
                ESP_LOGI(TAG, "Configuration mode requested");

                /*
                 * Consume any remaining characters from the input line,
                 * including ENTER, so the menu starts cleanly.
                 */
                while (true)
                {
                    int extra = getchar();

                    if (extra == EOF ||
                        extra == '\n' ||
                        extra == '\r')
                    {
                        break;
                    }
                }

                return true;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(poll_ms));
        elapsed_ms += poll_ms;
    }

    ESP_LOGI(TAG, "Configuration window expired");
    return false;
}