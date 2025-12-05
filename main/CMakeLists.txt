file(GLOB FONT_SOURCES "assets/fonts/*.c")
file(GLOB IMAGE_SOURCES "assets/images/*.c")
file(GLOB AUDIO_SOURCES "assets/audio/*.c")

set(AUDIO_DRIVER_SOURCES)
if(CONFIG_THEO_AUDIO_ENABLE)
    if(CONFIG_THEO_AUDIO_PIPELINE_NANO_BSP)
        list(APPEND AUDIO_DRIVER_SOURCES "thermostat/audio_driver_bsp_codec.c")
    elseif(CONFIG_THEO_AUDIO_PIPELINE_MAX98357)
        list(APPEND AUDIO_DRIVER_SOURCES "thermostat/audio_driver_max98357.c")
    endif()

    if(NOT AUDIO_DRIVER_SOURCES)
        message(FATAL_ERROR "No audio pipeline selected; enable THEO audio configs in menuconfig.")
    endif()
endif()

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
    "sensors/env_sensors.c"
    "thermostat_ui.c"
    "thermostat/ui_theme.c"
    "thermostat/ui_top_bar.c"
    "thermostat/ui_helpers.c"
    "thermostat/ui_setpoint_view.c"
    "thermostat/ui_setpoint_input.c"
    "thermostat/ui_actions.c"
    "thermostat/remote_setpoint_controller.c"
    "thermostat/backlight_manager.c"
    "thermostat/audio_boot.c"
    "thermostat/application_cues.c"
    "thermostat/thermostat_leds.c"
    "thermostat/thermostat_led_status.c"
    ${AUDIO_DRIVER_SOURCES}
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
