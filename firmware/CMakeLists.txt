# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "1.6.0")

# Add this line to disable the specific warning
add_compile_options(-Wno-missing-field-initializers)

option(STACKCHAN_LOCAL_ITO "Build local USB text-to-speech" OFF)
if(STACKCHAN_LOCAL_ITO)
    set(SDKCONFIG_DEFAULTS
        "${CMAKE_CURRENT_SOURCE_DIR}/sdkconfig.defaults;${CMAKE_CURRENT_SOURCE_DIR}/sdkconfig.ito.defaults"
        CACHE STRING "Local speech defaults" FORCE)
endif()

# Auto-load a local sdkconfig overlay (git-ignored) when present. Lets
# self-hosted / custom deployments pin CONFIG_STACKCHAN_SERVER_URL,
# CONFIG_OTA_URL, etc. without touching committed defaults.
# Manual equivalent: SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.local" idf.py build
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/sdkconfig.defaults.local")
    if(NOT STACKCHAN_LOCAL_ITO)
        set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_SOURCE_DIR}/sdkconfig.defaults")
    endif()
    set(SDKCONFIG_DEFAULTS
        "${SDKCONFIG_DEFAULTS};${CMAKE_CURRENT_SOURCE_DIR}/sdkconfig.defaults.local"
        CACHE STRING "ESP-IDF sdkconfig default files (auto-extended with local overlay)" FORCE)
    message(STATUS "StackChan: detected sdkconfig.defaults.local, applying overlay")
endif()

if(STACKCHAN_LOCAL_ITO)
    set(PROJECT_VER "1.6.0-ito")
    set(ITO_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/ito")
    if(NOT EXISTS "${ITO_SOURCE}/esp32/runtime/CMakeLists.txt")
        message(FATAL_ERROR "Run python3 fetch_repos.py to fetch the pinned Ito source")
    endif()
    list(APPEND EXTRA_COMPONENT_DIRS "${ITO_SOURCE}/esp32/runtime" "${ITO_SOURCE}/esp32/frontend")
endif()

add_definitions(-DFIRMWARE_VERSION=\"${PROJECT_VER}\")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
if(STACKCHAN_LOCAL_ITO)
    idf_build_set_property(DEPENDENCIES_LOCK "${CMAKE_CURRENT_SOURCE_DIR}/dependencies.ito.lock")
endif()
project(stack-chan)
