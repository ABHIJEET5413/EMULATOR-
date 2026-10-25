cmake_minimum_required(VERSION 3.21)

project(Sabi VERSION 2.0.0 LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Network Widgets Gui)

# Keep both executables in build/<Configuration>, matching the documented
# Phase 1 commands for Visual Studio's multi-configuration generator.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>")

add_library(sabi_core STATIC
    include/launch_channel.h
    src/launch_channel.cpp
    include/instance_manager.h
    include/instance_list_window.h
    include/settings_dialog.h
    src/instance_manager.cpp
    src/instance_list_window.cpp
    src/settings_dialog.cpp
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

target_include_directories(sabi_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(sabi_core
    PUBLIC
        Qt6::Core
        Qt6::Network
        Qt6::Widgets
        Qt6::Gui
)

if(WIN32)
    target_link_libraries(sabi_core PUBLIC user32)
endif()

add_executable(Sabi WIN32
    src/main.cpp
)

target_link_libraries(Sabi PRIVATE sabi_core)

# Regenerate the development default on configuration, replacing stale test copies.
file(GENERATE
    OUTPUT "${CMAKE_BINARY_DIR}/$<CONFIG>/vm/configs/default-instance.json"
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/../vm/configs/default-instance.json"
)

add_custom_command(TARGET Sabi POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -f "$<TARGET_FILE_DIR:Sabi>/DroidBox.exe"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/keymaps"
        "$<TARGET_FILE_DIR:Sabi>/resources/keymaps"
    COMMENT "Copying bundled keymap profiles"
)

# Keep the CLI harness in tests/ while allowing the documented command
# `cmake -S host-app` to configure both application and harness targets.
add_subdirectory(
    "${CMAKE_CURRENT_SOURCE_DIR}/../tests"
    "${CMAKE_CURRENT_BINARY_DIR}/tests"
)
