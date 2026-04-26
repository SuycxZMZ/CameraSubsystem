# 示例程序（非 GTest），统一输出到项目根目录 bin/

add_executable(camera_publisher_example
    camera_publisher_example.cpp
)

set_target_properties(camera_publisher_example PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CAMERA_SUBSYSTEM_RUNTIME_OUTPUT_DIR}"
)

target_link_libraries(camera_publisher_example
    PRIVATE
        camera_subsystem_ipc
        camera_subsystem_camera
        camera_subsystem_platform
        camera_subsystem_core
)

add_executable(camera_subscriber_example
    camera_subscriber_example.cpp
)

set_target_properties(camera_subscriber_example PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CAMERA_SUBSYSTEM_RUNTIME_OUTPUT_DIR}"
)

target_link_libraries(camera_subscriber_example
    PRIVATE
        camera_subsystem_ipc
        camera_subsystem_platform
        camera_subsystem_core
)

add_executable(dmabuf_smoke_test
    dmabuf_smoke_test.cpp
)

set_target_properties(dmabuf_smoke_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CAMERA_SUBSYSTEM_RUNTIME_OUTPUT_DIR}"
)

target_link_libraries(dmabuf_smoke_test
    PRIVATE
        camera_subsystem_camera
        camera_subsystem_platform
        camera_subsystem_core
)
