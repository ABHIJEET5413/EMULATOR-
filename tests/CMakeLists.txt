add_executable(test_vm_control
    test_vm_control.cpp
)

target_link_libraries(test_vm_control
    PRIVATE
        droidbox_core
        Qt6::Core
)

add_executable(test_display_input
    test_display_input.cpp
)

target_link_libraries(test_display_input
    PRIVATE
        droidbox_core
        Qt6::Core
)

add_executable(test_keymap
    test_keymap.cpp
)

target_link_libraries(test_keymap
    PRIVATE
        droidbox_core
        Qt6::Core
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
