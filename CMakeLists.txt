cmake_minimum_required(VERSION 3.16)
# Extension firmware template (firmware guide 6). Copy to firmware/<slug>, rename the project to
# yaykit_<slug with underscores> and the slug in main/main.c. Test builds may pass -DPROJECT_VER.
if(NOT DEFINED PROJECT_VER)
    set(PROJECT_VER "0.2.0")
endif()
# The shared firmware directory: in this repository the app sits in firmware/<slug>; in an app's own repository
# (the development kit's `kit new`) the kit is its devkit/ submodule.
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/devkit/firmware/components")
    set(YAYKIT_FIRMWARE "${CMAKE_CURRENT_LIST_DIR}/devkit/firmware")
else()
    set(YAYKIT_FIRMWARE "${CMAKE_CURRENT_LIST_DIR}/..")
endif()
set(EXTRA_COMPONENT_DIRS
    "${YAYKIT_FIRMWARE}/components/yaykit_app"
    "${YAYKIT_FIRMWARE}/components/yaykit_board"
    "${YAYKIT_FIRMWARE}/components/yaykit_key"
    "${YAYKIT_FIRMWARE}/components/yaykit_link"
    "${YAYKIT_FIRMWARE}/components/yaykit_ui"
    "${YAYKIT_FIRMWARE}/components/yaykit_wifi")
set(COMPONENTS main espcoredump)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
include(${YAYKIT_FIRMWARE}/reproducible.cmake)
project(yaykit_home_switch)
