idf_component_register(
    SRCS "rc_pd.c"
    INCLUDE_DIRS "include"
    REQUIRES rc_common driver esp_adc
)
