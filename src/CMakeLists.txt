idf_component_register(
    SRCS "app_main.cpp" "display.cpp" "pumps.cpp" "qr_serial.cpp" "recipes.cpp"
    INCLUDE_DIRS "."
    REQUIRES driver nvs_flash esp_wifi esp_http_client esp_event esp_netif esp_timer json spiffs
)
