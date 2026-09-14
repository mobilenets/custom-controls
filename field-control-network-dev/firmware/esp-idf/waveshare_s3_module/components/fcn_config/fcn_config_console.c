#include "fcn_config_console.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "fcn_config_console";

#define CONSOLE_LINE_MAX 128

static void trim_newline(char *text)
{
    if (text == NULL) {
        return;
    }

    size_t len = strlen(text);

    while (len > 0 &&
           (text[len - 1] == '\n' ||
            text[len - 1] == '\r'))
    {
        text[len - 1] = '\0';
        len--;
    }
}

static bool read_line(char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size < 2) {
        return false;
    }

    size_t pos = 0;

    while (true)
    {
        int ch = getchar();

        if (ch == EOF)
        {
            clearerr(stdin);
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        if (ch == '\r' || ch == '\n')
        {
            if (pos == 0)
            {
                continue;
            }

            buffer[pos] = '\0';
            return true;
        }

        if (pos < buffer_size - 1)
        {
            buffer[pos++] = (char)ch;
        }
    }
}

static void safe_copy(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0 || src == NULL) {
        return;
    }

    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

static const char *yes_no(bool value)
{
    return value ? "YES" : "NO";
}

static void print_page(
    const fcn_config_t *loaded,
    const fcn_config_t *edit)
{
    bool modified = !fcn_config_equal(loaded, edit);

    printf("\033[2J\033[H");
    printf("========== FCN CONFIGURATION ==========\n\n");

    printf(" 1. Module ID:          %u%s\n",
           edit->module_id,
           loaded->module_id != edit->module_id ? "  *" : "");

    printf(" 2. Module Name:        %s%s\n",
           edit->module_name,
           strcmp(loaded->module_name, edit->module_name) != 0 ? "  *" : "");

    printf(" 3. Input Count:        %u%s\n",
           edit->input_count,
           loaded->input_count != edit->input_count ? "  *" : "");

    printf(" 4. Output Count:       %u%s\n",
           edit->output_count,
           loaded->output_count != edit->output_count ? "  *" : "");

    printf("\n");

    printf(" 5. Agent IP:           %s%s\n",
           edit->agent_ip[0] ? edit->agent_ip : "[not configured]",
           strcmp(loaded->agent_ip, edit->agent_ip) != 0 ? "  *" : "");

    printf(" 6. Agent Port:         %u%s\n",
           edit->agent_port,
           loaded->agent_port != edit->agent_port ? "  *" : "");

    printf("\n");

    printf(" 7. Wi-Fi SSID:         %s%s\n",
           edit->wifi_ssid[0] ? edit->wifi_ssid : "[not configured]",
           strcmp(loaded->wifi_ssid, edit->wifi_ssid) != 0 ? "  *" : "");

    printf(" 8. Wi-Fi Password:     %s%s\n",
           edit->wifi_password[0] ? "[configured]" : "[not configured]",
           strcmp(loaded->wifi_password, edit->wifi_password) != 0 ? "  *" : "");

    printf(" 9. Ethernet Enabled:   %s%s\n",
           yes_no(edit->ethernet_enabled),
           loaded->ethernet_enabled != edit->ethernet_enabled ? "  *" : "");

    printf("10. Wi-Fi Fallback:     %s%s\n",
           yes_no(edit->wifi_fallback_enabled),
           loaded->wifi_fallback_enabled != edit->wifi_fallback_enabled ? "  *" : "");

    printf("\n---------------------------------------\n");
    printf("S. Save and reload\n");
    printf("C. Cancel\n");
    printf("---------------------------------------\n");

    if (modified) {
        printf("Configuration modified - not saved\n");
    } else {
        printf("No unsaved changes\n");
    }

    printf("\nSelection: ");
    fflush(stdout);
}

static bool parse_u32(const char *text, uint32_t *value)
{
    if (text == NULL || text[0] == '\0' || value == NULL) {
        return false;
    }

    char *end = NULL;
    unsigned long parsed = strtoul(text, &end, 10);

    if (end == text || *end != '\0') {
        return false;
    }

    *value = (uint32_t)parsed;
    return true;
}

static void edit_u8(
    const char *label,
    uint8_t *value,
    uint32_t min_value,
    uint32_t max_value)
{
    char line[CONSOLE_LINE_MAX];

    printf("\nCurrent %s: %u\n", label, *value);
    printf("New %s (ENTER keeps current): ", label);
    fflush(stdout);

    if (!read_line(line, sizeof(line)) || line[0] == '\0') {
        return;
    }

    uint32_t parsed;

    if (!parse_u32(line, &parsed) ||
        parsed < min_value ||
        parsed > max_value)
    {
        printf("Invalid value. Press ENTER.");
        read_line(line, sizeof(line));
        return;
    }

    *value = (uint8_t)parsed;
}

static void edit_u16(
    const char *label,
    uint16_t *value,
    uint32_t min_value,
    uint32_t max_value)
{
    char line[CONSOLE_LINE_MAX];

    printf("\nCurrent %s: %u\n", label, *value);
    printf("New %s (ENTER keeps current): ", label);
    fflush(stdout);

    if (!read_line(line, sizeof(line)) || line[0] == '\0') {
        return;
    }

    uint32_t parsed;

    if (!parse_u32(line, &parsed) ||
        parsed < min_value ||
        parsed > max_value)
    {
        printf("Invalid value. Press ENTER.");
        read_line(line, sizeof(line));
        return;
    }

    *value = (uint16_t)parsed;
}

static void edit_string(
    const char *label,
    char *value,
    size_t value_size,
    bool allow_empty)
{
    char line[CONSOLE_LINE_MAX];

    printf("\nCurrent %s: %s\n",
           label,
           value[0] ? value : "[not configured]");

    printf("New %s (ENTER keeps current): ", label);
    fflush(stdout);

    if (!read_line(line, sizeof(line))) {
        return;
    }

    if (line[0] == '\0') {
        return;
    }

    if (!allow_empty && strlen(line) == 0) {
        return;
    }

    safe_copy(value, value_size, line);
}

static void edit_password(
    char *value,
    size_t value_size)
{
    char line[CONSOLE_LINE_MAX];

    printf("\nWi-Fi Password: %s\n",
           value[0] ? "[configured]" : "[not configured]");

    printf("Enter new password.\n");
    printf("ENTER keeps current password.\n");
    printf("Type !CLEAR to erase it.\n");
    printf("New password: ");
    fflush(stdout);

    if (!read_line(line, sizeof(line))) {
        return;
    }

    if (line[0] == '\0') {
        return;
    }

    if (strcmp(line, "!CLEAR") == 0) {
        value[0] = '\0';
        return;
    }

    safe_copy(value, value_size, line);
}

static void toggle_bool(const char *label, bool *value)
{
    *value = !*value;

    printf("\n%s -> %s\n", label, yes_no(*value));
}

esp_err_t fcn_config_console_run(fcn_config_t *loaded_config)
{
    if (loaded_config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    fcn_config_t edit_config = *loaded_config;
    char line[CONSOLE_LINE_MAX];

    while (true)
    {
        print_page(loaded_config, &edit_config);

        if (!read_line(line, sizeof(line))) {
            ESP_LOGW(TAG, "Serial console input ended");
            return ESP_FAIL;
        }

        if (line[0] == '\0') {
            continue;
        }

        if (strlen(line) == 1)
        {
            char command = (char)toupper((unsigned char)line[0]);

            if (command == 'S')
            {
                if (!fcn_config_is_valid(&edit_config))
                {
                    printf("\nConfiguration is not valid. Press ENTER.");
                    read_line(line, sizeof(line));
                    continue;
                }

                esp_err_t err = fcn_config_save(&edit_config);

                if (err != ESP_OK)
                {
                    printf("\nSave failed: %s\nPress ENTER.",
                           esp_err_to_name(err));
                    read_line(line, sizeof(line));
                    continue;
                }

                err = fcn_config_reload(loaded_config);

                if (err != ESP_OK)
                {
                    printf("\nReload failed: %s\nPress ENTER.",
                           esp_err_to_name(err));
                    read_line(line, sizeof(line));
                    continue;
                }

                printf("\nConfiguration saved and reloaded.\n");
                return ESP_OK;
            }

            if (command == 'C')
            {
                printf("\nChanges cancelled. Nothing saved.\n");
                return ESP_ERR_INVALID_STATE;
            }
        }

        uint32_t selection;

        if (!parse_u32(line, &selection)) {
            continue;
        }

        switch (selection)
        {
            case 1:
                edit_u8("Module ID",
                        &edit_config.module_id,
                        1, 255);
                break;

            case 2:
                edit_string("Module Name",
                            edit_config.module_name,
                            sizeof(edit_config.module_name),
                            false);
                break;

            case 3:
                edit_u8("Input Count",
                        &edit_config.input_count,
                        0, 64);
                break;

            case 4:
                edit_u8("Output Count",
                        &edit_config.output_count,
                        0, 64);
                break;

            case 5:
                edit_string("Agent IP",
                            edit_config.agent_ip,
                            sizeof(edit_config.agent_ip),
                            true);
                break;

            case 6:
                edit_u16("Agent Port",
                         &edit_config.agent_port,
                         1, 65535);
                break;

            case 7:
                edit_string("Wi-Fi SSID",
                            edit_config.wifi_ssid,
                            sizeof(edit_config.wifi_ssid),
                            true);
                break;

            case 8:
                edit_password(
                    edit_config.wifi_password,
                    sizeof(edit_config.wifi_password)
                );
                break;

            case 9:
                toggle_bool(
                    "Ethernet Enabled",
                    &edit_config.ethernet_enabled
                );
                break;

            case 10:
                toggle_bool(
                    "Wi-Fi Fallback",
                    &edit_config.wifi_fallback_enabled
                );
                break;

            default:
                break;
        }
    }
}
