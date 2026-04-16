idf_component_register(
    SRCS "rc_motor.c" "drv8323.c"
    INCLUDE_DIRS "include"
    REQUIRES rc_common driver
)
