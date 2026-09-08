idf_component_register(
    SRCS
        "esp32_cam.c"
        "wifi.c"
        "web_server.c"

    INCLUDE_DIRS
        "."

    REQUIRES
        esp_http_server
        esp_wifi
        esp_netif
        esp_event
        nvs_flash
)
