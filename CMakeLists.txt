cmake_minimum_required(VERSION 3.24)
project(obs-virtual-mic VERSION 0.3.3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
find_package(Threads REQUIRED)

add_library(obs-virtual-mic MODULE
    src/plugin-main.cpp
    src/virtual-mic-controller.cpp
    src/virtual-mic-controller.hpp
    src/controls-integration.cpp
    src/controls-integration.hpp
    src/audio-router.cpp
    src/audio-router.hpp
    src/source-routing.cpp
    src/source-routing.hpp
    src/source-selection-dialog.cpp
    src/source-selection-dialog.hpp
    src/spsc-byte-ring.hpp
)

target_link_libraries(obs-virtual-mic PRIVATE
    OBS::libobs
    OBS::obs-frontend-api
    Qt6::Core
    Qt6::Widgets
    Threads::Threads
)

target_compile_definitions(obs-virtual-mic PRIVATE
    OBS_VIRTUAL_MIC_VERSION="${PROJECT_VERSION}"
)

target_compile_options(obs-virtual-mic PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(obs-virtual-mic PROPERTIES PREFIX "" OUTPUT_NAME "obs-virtual-mic")

include(GNUInstallDirs)
install(TARGETS obs-virtual-mic LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/obs-plugins)
