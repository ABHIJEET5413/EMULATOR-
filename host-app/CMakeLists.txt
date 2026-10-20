cmake_minimum_required(VERSION 3.21)

project(DroidBox VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Network)

# Keep both executables in build/<Configuration>, matching the documented
# Phase 1 commands for Visual Studio's multi-configuration generator.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>")

add_library(droidbox_core STATIC
    include/qemu_controller.h
    include/qmp_client.h
    src/qemu_controller.cpp
    src/qmp_client.cpp
)

target_include_directories(droidbox_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(droidbox_core
    PUBLIC
        Qt6::Core
        Qt6::Network
)

add_executable(DroidBox
    src/main.cpp
)

target_link_libraries(DroidBox PRIVATE droidbox_core)

# Keep the CLI harness in tests/ while allowing the documented command
# `cmake -S host-app` to configure both application and harness targets.
add_subdirectory(
    "${CMAKE_CURRENT_SOURCE_DIR}/../tests"
    "${CMAKE_CURRENT_BINARY_DIR}/tests"
)
