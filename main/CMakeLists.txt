file(GLOB FONT_SOURCES "assets/fonts/*.c")
file(GLOB IMAGE_SOURCES "assets/images/*.c")

# ESP-IDF replays this CMakeLists in script mode while collecting requirements,
# where set_source_files_properties() is disallowed. Gate it so only the real
# configure phase injects the override header.
if (NOT CMAKE_SCRIPT_MODE_FILE)
    set_source_files_properties(
        ${FONT_SOURCES} ${IMAGE_SOURCES}
        PROPERTIES COMPILE_OPTIONS "-include${CMAKE_CURRENT_SOURCE_DIR}/assets/lv_attr_overrides.h"
    )
endif()

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
