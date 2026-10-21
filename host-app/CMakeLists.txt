cmake_minimum_required(VERSION 3.21)

project(DroidBox VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Network Widgets Gui)

# Keep both executables in build/<Configuration>, matching the documented
# Phase 1 commands for Visual Studio's multi-configuration generator.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>")

add_library(droidbox_core STATIC
    include/adb_bridge.h
    include/display_embed.h
    include/input_handler.h
    include/keymap_engine.h
    include/keymap_profile.h
    include/main_window.h
    include/qemu_controller.h
    include/qmp_client.h
    src/display_embed.cpp
    src/adb_bridge.cpp
    src/input_handler.cpp
    src/keymap_engine.cpp
    src/keymap_profile.cpp
    src/main_window.cpp
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
        Qt6::Widgets
        Qt6::Gui
)

if(WIN32)
    target_link_libraries(droidbox_core PUBLIC user32)
endif()

add_executable(DroidBox
    src/main.cpp
)

target_link_libraries(DroidBox PRIVATE droidbox_core)

add_custom_command(TARGET DroidBox POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/keymaps"
        "$<TARGET_FILE_DIR:DroidBox>/resources/keymaps"
    COMMENT "Copying bundled keymap profiles"
)

# Keep the CLI harness in tests/ while allowing the documented command
# `cmake -S host-app` to configure both application and harness targets.
add_subdirectory(
    "${CMAKE_CURRENT_SOURCE_DIR}/../tests"
    "${CMAKE_CURRENT_BINARY_DIR}/tests"
)
