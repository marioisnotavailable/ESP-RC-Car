idf_component_register(
    SRCS "rc_comms.c"
    INCLUDE_DIRS "include"
    REQUIRES rc_common driver esp_wifi esp_http_server lwip nvs_flash esp_event json
)

# PlatformIO does not propagate the json component's transitive INCLUDE_DIRS.
# Use the cmake variable set by PlatformIO's IDF integration to locate cJSON.
target_include_directories(${COMPONENT_LIB} PRIVATE
    "${esp-idf_SOURCE_DIR}/components/json/cJSON"
)
