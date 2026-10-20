add_executable(test_vm_control
    test_vm_control.cpp
)

target_link_libraries(test_vm_control
    PRIVATE
        droidbox_core
        Qt6::Core
)
