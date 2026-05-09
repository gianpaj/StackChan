cmake_minimum_required(VERSION 3.16)

project(stackchan_avatar_sdl LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
set(LVGL_DIR "${REPO_ROOT}/firmware/managed_components/lvgl__lvgl")
set(SMOOTH_UI_TOOLKIT_DIR "${REPO_ROOT}/firmware/components/smooth_ui_toolkit")
set(FIRMWARE_MAIN_DIR "${REPO_ROOT}/firmware/main")

find_package(SDL2 REQUIRED)

set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_LIST_DIR}/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory("${LVGL_DIR}" "${CMAKE_BINARY_DIR}/lvgl")
target_include_directories(lvgl PUBLIC ${SDL2_INCLUDE_DIRS})
target_link_libraries(lvgl PUBLIC ${SDL2_LIBRARIES})

set(SMOOTH_UI_TOOLKIT_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SMOOTH_UI_TOOLKIT_BUILD_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory("${SMOOTH_UI_TOOLKIT_DIR}" "${CMAKE_BINARY_DIR}/smooth_ui_toolkit")

add_executable(avatar_sdl
    main.cpp
    "${FIRMWARE_MAIN_DIR}/stackchan/avatar/skins/default/default.cpp"
    "${FIRMWARE_MAIN_DIR}/stackchan/avatar/skins/default/eyes.cpp"
    "${FIRMWARE_MAIN_DIR}/stackchan/avatar/skins/default/mouth.cpp"
    "${FIRMWARE_MAIN_DIR}/stackchan/avatar/skins/default/speech_bubble.cpp"
    "${FIRMWARE_MAIN_DIR}/stackchan/avatar/skins/default/assets/default_bubble_arrow.c"
)

target_include_directories(avatar_sdl PRIVATE
    "${FIRMWARE_MAIN_DIR}"
    "${SMOOTH_UI_TOOLKIT_DIR}/src"
    "${SMOOTH_UI_TOOLKIT_DIR}/src/lvgl"
    ${SDL2_INCLUDE_DIRS}
)

target_link_libraries(avatar_sdl PRIVATE
    smooth_ui_toolkit
    lvgl
    ${SDL2_LIBRARIES}
)

if(APPLE)
    target_link_libraries(avatar_sdl PRIVATE "-framework CoreFoundation")
endif()
