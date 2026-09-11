#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_camera.h"
#include "esp_http_server.h"
#include "esp_log.h"

#include "web_server.h"

static const char *TAG = "web_server";

static httpd_handle_t main_server = NULL;
static httpd_handle_t stream_server = NULL;

/*
 * Prevent /capture and /stream from trying to use
 * the camera framebuffer at exactly the same time.
 */
static SemaphoreHandle_t camera_mutex = NULL;


/* ---------------------------------------------------------
 * MJPEG definitions
 * --------------------------------------------------------- */

#define PART_BOUNDARY "123456789000000000000987654321"

static const char *STREAM_CONTENT_TYPE =
    "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;

static const char *STREAM_BOUNDARY =
    "\r\n--" PART_BOUNDARY "\r\n";

static const char *STREAM_PART =
    "Content-Type: image/jpeg\r\n"
    "Content-Length: %u\r\n\r\n";


/* ---------------------------------------------------------
 * Main page
 * --------------------------------------------------------- */

static esp_err_t index_handler(httpd_req_t *req)
{
    const char *html =
        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "<title>FCN ESP32-CAM</title>"
        "<meta name=\"viewport\" "
        "content=\"width=device-width, initial-scale=1\">"

        "<style>"
        "body {"
        " font-family: sans-serif;"
        " margin: 20px;"
        " background: #202020;"
        " color: #eeeeee;"
        "}"
        "img {"
        " max-width: 100%;"
        " height: auto;"
        " border: 1px solid #666;"
        "}"
        "a {"
        " color: #80c0ff;"
        "}"
        "</style>"

        "</head>"

        "<body>"

        "<h1>Field Control Network</h1>"
        "<h2>ESP32-CAM Diagnostic Node</h2>"

        "<p>Live camera:</p>"

        "<img id=\"streamImage\">"

        "<p>"
        "<a href=\"/capture\" target=\"_blank\">"
        "Capture Still Image"
        "</a>"
        "</p>"

        "<script>"
        "document.getElementById('streamImage').src = "
        "'http://' + window.location.hostname + ':81/stream';"
        "</script>"

        "</body>"
        "</html>";

    httpd_resp_set_type(req, "text/html");

    return httpd_resp_send(
        req,
        html,
        HTTPD_RESP_USE_STRLEN
    );
}


/* ---------------------------------------------------------
 * Still image handler
 * Port 80
 * --------------------------------------------------------- */

static esp_err_t capture_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Still image requested");

    /*
     * Wait for the camera to become available.
     */
    if (xSemaphoreTake(
            camera_mutex,
            pdMS_TO_TICKS(2000)) != pdTRUE) {

        ESP_LOGE(TAG, "Camera busy");

        httpd_resp_set_status(
            req,
            "503 Service Unavailable"
        );

        httpd_resp_set_type(
            req,
            "text/plain"
        );

        httpd_resp_send(
            req,
            "Camera busy",
            HTTPD_RESP_USE_STRLEN
        );

        return ESP_FAIL;
    }

    camera_fb_t *fb = esp_camera_fb_get();

    if (fb == NULL) {

        xSemaphoreGive(camera_mutex);

        ESP_LOGE(TAG, "Camera capture failed");

        httpd_resp_send_500(req);

        return ESP_FAIL;
    }

    ESP_LOGI(
        TAG,
        "Still JPEG: %u bytes, %ux%u",
        (unsigned)fb->len,
        fb->width,
        fb->height
    );

    esp_err_t res =
        httpd_resp_set_type(
            req,
            "image/jpeg"
        );

    if (res == ESP_OK) {
        res =
            httpd_resp_set_hdr(
                req,
                "Content-Disposition",
                "inline; filename=capture.jpg"
            );
    }

    if (res == ESP_OK) {
        res =
            httpd_resp_set_hdr(
                req,
                "Cache-Control",
                "no-store"
            );
    }

    if (res == ESP_OK) {
        res =
            httpd_resp_send(
                req,
                (const char *)fb->buf,
                fb->len
            );
    }

    esp_camera_fb_return(fb);

    xSemaphoreGive(camera_mutex);

    return res;
}


/* ---------------------------------------------------------
 * MJPEG stream handler
 * Port 81
 * --------------------------------------------------------- */

static esp_err_t stream_handler(httpd_req_t *req)
{
    esp_err_t res;

    res =
        httpd_resp_set_type(
            req,
            STREAM_CONTENT_TYPE
        );

    if (res != ESP_OK) {
        return res;
    }

    httpd_resp_set_hdr(
        req,
        "Cache-Control",
        "no-store"
    );

    ESP_LOGI(TAG, "MJPEG stream client connected");

    while (1) {

        /*
         * Take ownership of camera for one frame.
         *
         * Short timeout is intentional. If /capture currently
         * owns the camera, simply skip this stream frame.
         */
        if (xSemaphoreTake(
                camera_mutex,
                pdMS_TO_TICKS(500)) != pdTRUE) {

            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        camera_fb_t *fb =
            esp_camera_fb_get();

        if (fb == NULL) {

            xSemaphoreGive(camera_mutex);

            ESP_LOGE(
                TAG,
                "Stream camera capture failed"
            );

            res = ESP_FAIL;
            break;
        }

        if (fb->format != PIXFORMAT_JPEG) {

            ESP_LOGE(
                TAG,
                "Stream frame is not JPEG"
            );

            esp_camera_fb_return(fb);

            xSemaphoreGive(camera_mutex);

            res = ESP_FAIL;
            break;
        }

        char part_header[64];

        int header_length =
            snprintf(
                part_header,
                sizeof(part_header),
                STREAM_PART,
                (unsigned)fb->len
            );

        res =
            httpd_resp_send_chunk(
                req,
                STREAM_BOUNDARY,
                strlen(STREAM_BOUNDARY)
            );

        if (res == ESP_OK) {

            res =
                httpd_resp_send_chunk(
                    req,
                    part_header,
                    header_length
                );
        }

        if (res == ESP_OK) {

            res =
                httpd_resp_send_chunk(
                    req,
                    (const char *)fb->buf,
                    fb->len
                );
        }

        esp_camera_fb_return(fb);

        xSemaphoreGive(camera_mutex);

        /*
         * Browser disconnected or network error.
         */
        if (res != ESP_OK) {

            ESP_LOGI(
                TAG,
                "MJPEG stream client disconnected"
            );

            break;
        }

        /*
         * Diagnostic camera:
         * approximately 10 FPS maximum.
         */
        vTaskDelay(
            pdMS_TO_TICKS(100)
        );
    }

    return res;
}


/* ---------------------------------------------------------
 * Start both HTTP servers
 * --------------------------------------------------------- */

esp_err_t web_server_start(void)
{
    /*
     * Create camera lock once.
     */
    if (camera_mutex == NULL) {

        camera_mutex =
            xSemaphoreCreateMutex();

        if (camera_mutex == NULL) {

            ESP_LOGE(
                TAG,
                "Failed to create camera mutex"
            );

            return ESP_ERR_NO_MEM;
        }
    }


    /* =====================================================
     * Main HTTP server - port 80
     * ===================================================== */

    httpd_config_t main_config =
        HTTPD_DEFAULT_CONFIG();

    main_config.server_port = 80;

    ESP_LOGI(
        TAG,
        "Starting main HTTP server on port %u",
        main_config.server_port
    );

    esp_err_t res =
        httpd_start(
            &main_server,
            &main_config
        );

    if (res != ESP_OK) {

        ESP_LOGE(
            TAG,
            "Main HTTP server failed: %s",
            esp_err_to_name(res)
        );

        return res;
    }


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


    ESP_ERROR_CHECK(
        httpd_register_uri_handler(
            main_server,
            &index_uri
        )
    );


    ESP_ERROR_CHECK(
        httpd_register_uri_handler(
            main_server,
            &capture_uri
        )
    );


    /* =====================================================
     * Stream HTTP server - port 81
     * ===================================================== */

    httpd_config_t stream_config =
        HTTPD_DEFAULT_CONFIG();

    /*
     * BOTH ports must differ from the first server.
     */
    stream_config.server_port = 81;

    stream_config.ctrl_port =
        main_config.ctrl_port + 1;


    ESP_LOGI(
        TAG,
        "Starting stream HTTP server on port %u",
        stream_config.server_port
    );


    res =
        httpd_start(
            &stream_server,
            &stream_config
        );

    if (res != ESP_OK) {

        ESP_LOGE(
            TAG,
            "Stream HTTP server failed: %s",
            esp_err_to_name(res)
        );

        httpd_stop(main_server);
        main_server = NULL;

        return res;
    }


    httpd_uri_t stream_uri = {
        .uri       = "/stream",
        .method    = HTTP_GET,
        .handler   = stream_handler,
        .user_ctx  = NULL
    };


    ESP_ERROR_CHECK(
        httpd_register_uri_handler(
            stream_server,
            &stream_uri
        )
    );


    ESP_LOGI(
        TAG,
        "HTTP servers ready"
    );

    ESP_LOGI(
        TAG,
        "Main server   : port 80"
    );

    ESP_LOGI(
        TAG,
        "Stream server : port 81"
    );


    return ESP_OK;
}