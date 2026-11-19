idf_component_register(
    SRCS
        "main.cc"
    INCLUDE_DIRS
        "."
    REQUIRES esp_bsp esp_lv_port lvgl
)
