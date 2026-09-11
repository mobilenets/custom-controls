idf_component_register(
    SRCS
        "esp32_cam.c"
        "wifi.c"
        "web_server.c"
        "micro_ros_node.c"

    INCLUDE_DIRS
        "."

    REQUIRES
        esp_http_server
        esp_wifi
        esp_netif
        esp_event
        nvs_flash
        micro_ros_espidf_component
)