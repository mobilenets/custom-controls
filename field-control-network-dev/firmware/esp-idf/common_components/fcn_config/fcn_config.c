#include "fcn_config.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_check.h"
static const char *TAG = "fcn_config";
static const char *NVS_NAMESPACE = "wifi";

/*
 * These key names intentionally preserve the earlier FCN naming where useful.
 * NVS key names must remain short.
 */
#define NVS_KEY_SSID          "ssid"
#define NVS_KEY_PASSWORD      "password"
#define NVS_KEY_AGENT_IP      "agentIp"
#define NVS_KEY_AGENT_PORT    "agentPort"
#define NVS_KEY_MODULE_ID     "moduleId"
#define NVS_KEY_MODULE_NAME   "modName"
#define NVS_KEY_INPUT_COUNT   "inCount"
#define NVS_KEY_OUTPUT_COUNT  "outCount"
#define NVS_KEY_ETH_ENABLED   "ethEn"
#define NVS_KEY_WIFI_FALLBACK "wifiFb"

static esp_err_t ensure_nvs_ready(void)
{
    esp_err_t err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGW(TAG, "NVS requires erase/reinitialize");

        ESP_RETURN_ON_ERROR(
            nvs_flash_erase(),
            TAG,
            "nvs_flash_erase failed"
        );

        err = nvs_flash_init();
    }

    return err;
}

static void copy_string(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }

    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

void fcn_config_set_defaults(fcn_config_t *config)
{
    if (config == NULL) {
        return;
    }

    memset(config, 0, sizeof(*config));

    config->agent_port = 8888;
    config->module_id = 1;

    copy_string(
        config->module_name,
        sizeof(config->module_name),
        "FCN Module"
    );

    /*
     * Current Waveshare board defaults.
     * These remain configurable so the common FCN configuration object
     * can also represent future modules and optional expansion hardware.
     */
    config->input_count = 8;
    config->output_count = 8;

    config->ethernet_enabled = true;
    config->wifi_fallback_enabled = true;
}

static void read_string_if_present(
    nvs_handle_t handle,
    const char *key,
    char *destination,
    size_t destination_size)
{
    size_t required_size = destination_size;

    esp_err_t err = nvs_get_str(
        handle,
        key,
        destination,
        &required_size
    );

    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Could not read NVS key '%s': %s",
                 key, esp_err_to_name(err));
    }
}

esp_err_t fcn_config_load(fcn_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(
        ensure_nvs_ready(),
        TAG,
        "NVS initialization failed"
    );

    fcn_config_set_defaults(config);

    nvs_handle_t handle;

    esp_err_t err = nvs_open(
        NVS_NAMESPACE,
        NVS_READONLY,
        &handle
    );

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No saved FCN configuration; using defaults");
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(
        err,
        TAG,
        "Unable to open FCN NVS namespace"
    );

    read_string_if_present(
        handle,
        NVS_KEY_SSID,
        config->wifi_ssid,
        sizeof(config->wifi_ssid)
    );

    read_string_if_present(
        handle,
        NVS_KEY_PASSWORD,
        config->wifi_password,
        sizeof(config->wifi_password)
    );

    read_string_if_present(
        handle,
        NVS_KEY_AGENT_IP,
        config->agent_ip,
        sizeof(config->agent_ip)
    );

    read_string_if_present(
        handle,
        NVS_KEY_MODULE_NAME,
        config->module_name,
        sizeof(config->module_name)
    );

    uint16_t u16_value;
    uint8_t u8_value;

    if (nvs_get_u16(handle, NVS_KEY_AGENT_PORT, &u16_value) == ESP_OK) {
        config->agent_port = u16_value;
    }

    if (nvs_get_u8(handle, NVS_KEY_MODULE_ID, &u8_value) == ESP_OK) {
        config->module_id = u8_value;
    }

    if (nvs_get_u8(handle, NVS_KEY_INPUT_COUNT, &u8_value) == ESP_OK) {
        config->input_count = u8_value;
    }

    if (nvs_get_u8(handle, NVS_KEY_OUTPUT_COUNT, &u8_value) == ESP_OK) {
        config->output_count = u8_value;
    }

    if (nvs_get_u8(handle, NVS_KEY_ETH_ENABLED, &u8_value) == ESP_OK) {
        config->ethernet_enabled = (u8_value != 0);
    }

    if (nvs_get_u8(handle, NVS_KEY_WIFI_FALLBACK, &u8_value) == ESP_OK) {
        config->wifi_fallback_enabled = (u8_value != 0);
    }

    nvs_close(handle);

    ESP_LOGI(TAG, "FCN configuration loaded");
    return ESP_OK;
}

esp_err_t fcn_config_save(const fcn_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!fcn_config_is_valid(config)) {
        ESP_LOGE(TAG, "Configuration validation failed");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(
        ensure_nvs_ready(),
        TAG,
        "NVS initialization failed"
    );

    nvs_handle_t handle;

    ESP_RETURN_ON_ERROR(
        nvs_open(
            NVS_NAMESPACE,
            NVS_READWRITE,
            &handle
        ),
        TAG,
        "Unable to open FCN NVS namespace"
    );

    esp_err_t err = ESP_OK;

#define FCN_NVS_CHECK(expr)                  \
    do {                                     \
        err = (expr);                        \
        if (err != ESP_OK) {                 \
            goto save_failed;                \
        }                                    \
    } while (0)

    FCN_NVS_CHECK(nvs_set_str(handle, NVS_KEY_SSID,
                              config->wifi_ssid));

    FCN_NVS_CHECK(nvs_set_str(handle, NVS_KEY_PASSWORD,
                              config->wifi_password));

    FCN_NVS_CHECK(nvs_set_str(handle, NVS_KEY_AGENT_IP,
                              config->agent_ip));

    FCN_NVS_CHECK(nvs_set_u16(handle, NVS_KEY_AGENT_PORT,
                              config->agent_port));

    FCN_NVS_CHECK(nvs_set_u8(handle, NVS_KEY_MODULE_ID,
                             config->module_id));

    FCN_NVS_CHECK(nvs_set_str(handle, NVS_KEY_MODULE_NAME,
                              config->module_name));

    FCN_NVS_CHECK(nvs_set_u8(handle, NVS_KEY_INPUT_COUNT,
                             config->input_count));

    FCN_NVS_CHECK(nvs_set_u8(handle, NVS_KEY_OUTPUT_COUNT,
                             config->output_count));

    FCN_NVS_CHECK(nvs_set_u8(handle, NVS_KEY_ETH_ENABLED,
                             config->ethernet_enabled ? 1 : 0));

    FCN_NVS_CHECK(nvs_set_u8(handle, NVS_KEY_WIFI_FALLBACK,
                             config->wifi_fallback_enabled ? 1 : 0));

    FCN_NVS_CHECK(nvs_commit(handle));

    nvs_close(handle);

    ESP_LOGI(TAG, "FCN configuration saved");
    return ESP_OK;

save_failed:
    ESP_LOGE(TAG, "Failed saving FCN configuration: %s",
             esp_err_to_name(err));
    nvs_close(handle);
    return err;

#undef FCN_NVS_CHECK
}

esp_err_t fcn_config_reload(fcn_config_t *config)
{
    return fcn_config_load(config);
}

bool fcn_config_wifi_available(const fcn_config_t *config)
{
    if (config == NULL) {
        return false;
    }

    return config->wifi_ssid[0] != '\0';
}

bool fcn_config_is_valid(const fcn_config_t *config)
{
    if (config == NULL) {
        return false;
    }

    if (config->agent_port == 0) {
        return false;
    }

    if (config->module_id == 0) {
        return false;
    }

    if (config->module_name[0] == '\0') {
        return false;
    }

    if (config->input_count > 64 ||
        config->output_count > 64)
    {
        return false;
    }

    /*
     * Agent IP may be empty during initial commissioning.
     * Wi-Fi credentials are optional on Ethernet-capable boards.
     */

    return true;
}

bool fcn_config_equal(const fcn_config_t *a, const fcn_config_t *b)
{
    if (a == NULL || b == NULL) {
        return false;
    }

    return memcmp(a, b, sizeof(*a)) == 0;
}