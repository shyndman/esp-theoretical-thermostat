set(THEO_UI_SOURCES
    "app_main.c"
    "thermostat_ui.c"
    "assets/images/sunny.c"
    "assets/images/room_default.c"
    "assets/images/power.c"
    "assets/images/snowflake.c"
    "assets/images/fire.c"
    "assets/images/fan.c"
)

idf_component_register(
    SRCS ${THEO_UI_SOURCES}
    INCLUDE_DIRS "."
    REQUIRES esp_lvgl_adapter lvgl
)
