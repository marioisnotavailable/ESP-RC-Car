idf_component_register(
    SRCS "rc_common.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos esp_driver_gpio joltwallet__littlefs
)
