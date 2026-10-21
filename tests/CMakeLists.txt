add_executable(test_vm_control
    test_vm_control.cpp
)

add_executable(test_launch_channel test_launch_channel.cpp)
target_link_libraries(test_launch_channel PRIVATE droidbox_core)
add_test(NAME launch_channel COMMAND test_launch_channel)
set_tests_properties(launch_channel PROPERTIES TIMEOUT 45)

if(WIN32)
    add_test(NAME installer_preflight COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_CURRENT_SOURCE_DIR}/test_installer.ps1")
    add_test(NAME whpx_script COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_CURRENT_SOURCE_DIR}/test_whpx.ps1")
    set_tests_properties(whpx_script PROPERTIES TIMEOUT 90)
endif()

target_link_libraries(test_vm_control
    PRIVATE
        droidbox_core
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
        droidbox_core
        Qt6::Core
)

add_test(NAME display_input COMMAND test_display_input)
set_tests_properties(display_input PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

add_executable(test_keymap
    test_keymap.cpp
)

target_link_libraries(test_keymap
    PRIVATE
        droidbox_core
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
        droidbox_core
        Qt6::Core
)

add_test(NAME adb COMMAND test_adb)
set_tests_properties(adb PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

# Explicit opt-in only: this harness installs an APK into a running Android guest.
add_executable(test_adb_live test_adb_live.cpp)
target_link_libraries(test_adb_live PRIVATE droidbox_core Qt6::Core)

add_executable(test_instance_manager test_instance_manager.cpp)
target_link_libraries(test_instance_manager PRIVATE droidbox_core)
add_test(NAME instance_manager COMMAND test_instance_manager)
set_tests_properties(instance_manager PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

add_executable(test_phase5_ui test_phase5_ui.cpp)
target_link_libraries(test_phase5_ui PRIVATE droidbox_core)
add_test(NAME phase5_ui COMMAND test_phase5_ui)
set_tests_properties(phase5_ui PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 90
)

if(WIN32)
    add_executable(test_native_keyboard test_native_keyboard.cpp)
    target_link_libraries(test_native_keyboard PRIVATE droidbox_core)
    add_test(NAME native_keyboard COMMAND test_native_keyboard)
    set_tests_properties(native_keyboard PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
        ENVIRONMENT "QT_QPA_PLATFORM=windows"
        TIMEOUT 45
    )
endif()
