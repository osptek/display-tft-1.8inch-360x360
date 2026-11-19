idf_component_register(
    SRCS "esp_bsp.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "priv_include"
    REQUIRES esp_driver_gpio esp_driver_i2c
    PRIV_REQUIRES esp_driver_spi esp_lcd esp_lcd_jd9855 esp_lcd_touch_cst816 esp_lv_port 
)
