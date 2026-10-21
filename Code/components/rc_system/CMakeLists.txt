idf_component_register(
    SRCS "rc_system.c"
    INCLUDE_DIRS "include"
    REQUIRES rc_common driver nvs_flash esp_adc esp_system
)
