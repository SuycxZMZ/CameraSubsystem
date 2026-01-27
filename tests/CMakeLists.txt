# 查找 Google Test（未安装时自动降级为跳过测试）
find_package(GTest QUIET)

if (NOT GTest_FOUND)
    message(WARNING "GTest not found; skipping unit tests")
    return()
endif ()

# 单元测试
add_executable(test_frame_handle
    unit/test_frame_handle.cpp
)

target_link_libraries(test_frame_handle
    PRIVATE
        camera_subsystem_core
        GTest::GTest
        GTest::Main
)

add_test(NAME test_frame_handle COMMAND test_frame_handle)

add_executable(test_camera_config
    unit/test_camera_config.cpp
)

target_link_libraries(test_camera_config
    PRIVATE
        camera_subsystem_core
        GTest::GTest
        GTest::Main
)

add_test(NAME test_camera_config COMMAND test_camera_config)
