idf_component_register(
    SRCS "rc_bg.c"
    INCLUDE_DIRS "include"
    REQUIRES rc_common rc_system esp_https_ota esp_http_client app_update
)
