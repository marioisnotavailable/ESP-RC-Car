idf_component_register(
    SRCS "rc_system.c"
    INCLUDE_DIRS "include"
    REQUIRES rc_common esp_driver_gpio esp_driver_ledc nvs_flash esp_adc esp_system
)
