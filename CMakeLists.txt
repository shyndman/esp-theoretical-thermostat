# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.10)

set(SDKCONFIG_DEFAULTS
    "sdkconfig.defaults")

if(EXISTS "${CMAKE_SOURCE_DIR}/sdkconfig.defaults.local")
  list(APPEND SDKCONFIG_DEFAULTS "sdkconfig.defaults.local")
else()
  message(STATUS "sdkconfig.defaults.local not found; skipping")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp_theoretical_thermostat)
