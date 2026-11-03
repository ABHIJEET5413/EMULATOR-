add_executable(test_vm_control
    test_vm_control.cpp
)

add_executable(test_launch_channel test_launch_channel.cpp)
target_link_libraries(test_launch_channel PRIVATE sabi_core)
add_test(NAME launch_channel COMMAND test_launch_channel)
set_tests_properties(launch_channel PROPERTIES TIMEOUT 45)

if(WIN32)
    add_test(NAME installer_preflight COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_CURRENT_SOURCE_DIR}/test_installer.ps1")
    add_test(NAME whpx_script COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_CURRENT_SOURCE_DIR}/test_whpx.ps1")
    add_test(NAME release_readiness COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_CURRENT_SOURCE_DIR}/test_release_readiness.ps1")
    set_tests_properties(whpx_script PROPERTIES TIMEOUT 90)
endif()

target_link_libraries(test_vm_control
    PRIVATE
        sabi_core
        Qt6::Core
)

add_test(NAME vm_control_config COMMAND test_vm_control --test config_load)
set_tests_properties(vm_control_config PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

add_executable(test_display_input
    test_display_input.cpp
)

target_link_libraries(test_display_input
    PRIVATE
        sabi_core
        Qt6::Core
)

add_test(NAME display_input COMMAND test_display_input)
set_tests_properties(display_input PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

add_executable(test_viewport_input test_viewport_input.cpp)
target_link_libraries(test_viewport_input PRIVATE sabi_core Qt6::Core)
add_test(NAME viewport_input COMMAND test_viewport_input)
set_tests_properties(viewport_input PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

add_executable(test_keymap
    test_keymap.cpp
)

target_link_libraries(test_keymap
    PRIVATE
        sabi_core
        Qt6::Core
)

add_test(NAME keymap COMMAND test_keymap)
set_tests_properties(keymap PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

add_custom_command(TARGET test_keymap POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/../host-app/resources/keymaps"
        "$<TARGET_FILE_DIR:test_keymap>/keymaps"
    COMMENT "Copying keymap profiles for the test harness"
)

add_executable(test_adb
    test_adb.cpp
)

target_link_libraries(test_adb
    PRIVATE
        sabi_core
        Qt6::Core
)

add_test(NAME adb COMMAND test_adb)
set_tests_properties(adb PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

# Explicit opt-in only: this harness installs an APK into a running Android guest.
add_executable(test_adb_live test_adb_live.cpp)
target_link_libraries(test_adb_live PRIVATE sabi_core Qt6::Core)

add_executable(test_instance_manager test_instance_manager.cpp)
target_link_libraries(test_instance_manager PRIVATE sabi_core)
add_test(NAME instance_manager COMMAND test_instance_manager)
set_tests_properties(instance_manager PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

add_executable(test_phase5_ui test_phase5_ui.cpp)
target_link_libraries(test_phase5_ui PRIVATE sabi_core)
add_test(NAME phase5_ui COMMAND test_phase5_ui)
set_tests_properties(phase5_ui PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 90
)

add_executable(test_phase7_ui test_phase7_ui.cpp)
target_link_libraries(test_phase7_ui PRIVATE sabi_core Qt6::Core Qt6::Gui Qt6::Widgets)
add_test(NAME phase7_ui COMMAND test_phase7_ui)
set_tests_properties(phase7_ui PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 45
)

add_executable(test_control_center test_control_center.cpp)
target_link_libraries(test_control_center PRIVATE sabi_core Qt6::Core Qt6::Gui Qt6::Widgets)
add_test(NAME control_center COMMAND test_control_center)
set_tests_properties(control_center PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 45
)

add_executable(test_keymap_studio test_keymap_studio.cpp)
target_link_libraries(test_keymap_studio PRIVATE sabi_core Qt6::Core)
add_test(NAME keymap_studio COMMAND test_keymap_studio)
set_tests_properties(keymap_studio PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
    TIMEOUT 45
)

add_executable(test_instance_workspace test_instance_workspace.cpp)
target_link_libraries(test_instance_workspace PRIVATE sabi_core Qt6::Core)
add_test(NAME instance_workspace COMMAND test_instance_workspace)
set_tests_properties(instance_workspace PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
    TIMEOUT 45
)

add_executable(test_runtime_recovery test_runtime_recovery.cpp)
target_link_libraries(test_runtime_recovery PRIVATE sabi_core Qt6::Core)
add_test(NAME runtime_recovery COMMAND test_runtime_recovery)
set_tests_properties(runtime_recovery PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
    TIMEOUT 45
)

if(WIN32)
    add_executable(test_native_keyboard test_native_keyboard.cpp)
    target_link_libraries(test_native_keyboard PRIVATE sabi_core)
    add_test(NAME native_keyboard COMMAND test_native_keyboard)
    set_tests_properties(native_keyboard PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
        ENVIRONMENT "QT_QPA_PLATFORM=windows"
        TIMEOUT 45
    )
endif()
