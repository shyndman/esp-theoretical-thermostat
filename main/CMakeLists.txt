file(GLOB FONT_SOURCES "assets/fonts/*.c")
file(GLOB IMAGE_SOURCES "assets/images/*.c")

set(THEO_UI_SOURCES
    "app_main.c"
    "thermostat_ui.c"
    "thermostat/ui_theme.c"
    "thermostat/ui_top_bar.c"
    "thermostat/ui_setpoint_view.c"
    "thermostat/ui_setpoint_input.c"
    "thermostat/ui_actions.c"
    ${IMAGE_SOURCES}
    ${FONT_SOURCES}
)

idf_component_register(
    SRCS ${THEO_UI_SOURCES}
    INCLUDE_DIRS "."
    REQUIRES esp_lvgl_adapter lvgl
)
