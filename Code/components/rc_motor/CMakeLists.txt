idf_component_register(
    SRCS "rc_motor.c" "drv8323.c"
    INCLUDE_DIRS "include"
    REQUIRES rc_common esp_driver_gpio esp_driver_spi esp_driver_ledc esp_timer
)
