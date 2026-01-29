
idf_component_register(
    SRCS "esp32_p4_nano.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "priv_include"
    REQUIRES driver esp_codec_dev
    PRIV_REQUIRES esp_lcd usb spiffs fatfs
)
