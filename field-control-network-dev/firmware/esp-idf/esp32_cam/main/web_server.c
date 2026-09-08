#include <stdio.h>

#include "esp_camera.h"
#include "esp_http_server.h"
#include "esp_log.h"

#include "web_server.h"

static const char *TAG = "web_server";

static httpd_handle_t server = NULL;


/*
 * Simple home page
 */
static esp_err_t index_handler(httpd_req_t *req)
{
    const char *html =
        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "<title>FCN ESP32-CAM</title>"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
        "</head>"
        "<body>"
        "<h1>Field Control Network</h1>"
        "<h2>ESP32-CAM Diagnostic Node</h2>"
        "<p>Camera server is running.</p>"
        "<p><a href=\"/capture\">Capture Image</a></p>"
        "<p><img src=\"/capture\" style=\"max-width:100%;\"></p>"
        "</body>"
        "</html>";

    httpd_resp_set_type(req, "text/html");

    return httpd_resp_send(
        req,
        html,
        HTTPD_RESP_USE_STRLEN
    );
}


/*
 * Capture a fresh JPEG and return it directly
 * to the browser.
 */
static esp_err_t capture_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Capturing image...");

    camera_fb_t *fb = esp_camera_fb_get();

    if (fb == NULL) {
        ESP_LOGE(TAG, "Camera capture failed");

        httpd_resp_send_500(req);

        return ESP_FAIL;
    }

    /*
     * Our camera is configured for JPEG,
     * but verify it anyway.
     */
    if (fb->format != PIXFORMAT_JPEG) {
        ESP_LOGE(TAG, "Captured frame is not JPEG");

        esp_camera_fb_return(fb);

        httpd_resp_send_500(req);

        return ESP_FAIL;
    }

    ESP_LOGI(
        TAG,
        "JPEG captured: %u bytes, %ux%u",
        (unsigned)fb->len,
        fb->width,
        fb->height
    );

    esp_err_t result =
        httpd_resp_set_type(req, "image/jpeg");

    if (result == ESP_OK) {
        result =
            httpd_resp_set_hdr(
                req,
                "Content-Disposition",
                "inline; filename=capture.jpg"
            );
    }

    if (result == ESP_OK) {
        result =
            httpd_resp_send(
                req,
                (const char *)fb->buf,
                fb->len
            );
    }

    esp_camera_fb_return(fb);

    return result;
}


esp_err_t web_server_start(void)
{
    httpd_config_t config =
        HTTPD_DEFAULT_CONFIG();

    config.server_port = 80;

    httpd_uri_t index_uri = {
        .uri       = "/",
        .method    = HTTP_GET,
        .handler   = index_handler,
        .user_ctx  = NULL
    };

    httpd_uri_t capture_uri = {
        .uri       = "/capture",
        .method    = HTTP_GET,
        .handler   = capture_handler,
        .user_ctx  = NULL
    };

    ESP_LOGI(
        TAG,
        "Starting HTTP server on port %d",
        config.server_port
    );

    esp_err_t result =
        httpd_start(&server, &config);

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "HTTP server failed to start: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    ESP_ERROR_CHECK(
        httpd_register_uri_handler(
            server,
            &index_uri
        )
    );

    ESP_ERROR_CHECK(
        httpd_register_uri_handler(
            server,
            &capture_uri
        )
    );

    ESP_LOGI(TAG, "HTTP server started");

    return ESP_OK;
}