file(GLOB FONT_SOURCES "assets/fonts/*.c")
file(GLOB IMAGE_SOURCES "assets/images/*.c")
file(GLOB AUDIO_SOURCES "assets/audio/*.c")

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
    "connectivity/esp_hosted_link.c"
    "connectivity/wifi_remote_manager.c"
    "connectivity/time_sync.c"
    "connectivity/mqtt_manager.c"
    "connectivity/mqtt_dataplane.c"
    "thermostat_ui.c"
    "thermostat/ui_theme.c"
    "thermostat/ui_top_bar.c"
    "thermostat/ui_setpoint_view.c"
    "thermostat/ui_setpoint_input.c"
    "thermostat/ui_actions.c"
    "thermostat/backlight_manager.c"
    "thermostat/audio_boot.c"
    "thermostat/ui_splash.c"
    ${IMAGE_SOURCES}
    ${FONT_SOURCES}
    ${AUDIO_SOURCES}
)

idf_component_register(
    SRCS ${THEO_UI_SOURCES}
    INCLUDE_DIRS "."
    REQUIRES esp_lvgl_adapter lvgl esp_wifi_remote esp_hosted esp_netif nvs_flash esp_wifi mqtt
)
