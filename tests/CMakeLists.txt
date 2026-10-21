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
