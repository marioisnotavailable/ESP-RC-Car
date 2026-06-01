idf_component_register(
    SRCS "rc_comms.c"
    INCLUDE_DIRS "include"
    REQUIRES rc_common rc_system esp_driver_gpio esp_wifi esp_http_server lwip nvs_flash esp_event cjson
)
