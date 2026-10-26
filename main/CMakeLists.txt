idf_component_register(SRCS "app_main.c" "factory_codec.c" "bulb_store.c" "bulb_http.c" "bulb_led.c" "bulb_clock.c" "bulb_country.c"
    INCLUDE_DIRS "."
    REQUIRES matter_c matter_c_esp32 esp_wifi esp_event esp_http_server esp_timer esp_driver_gpio esp_driver_rmt app_update esp_app_format)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror -fstack-usage)
string(TIMESTAMP bulb_build_utc "%s" UTC)
target_compile_definitions(${COMPONENT_LIB} PRIVATE BULB_BUILD_UTC=${bulb_build_utc})
