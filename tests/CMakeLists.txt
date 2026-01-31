# 压测程序：不依赖 GTest，始终构建
add_executable(platform_stress_test
    stress/platform_stress_test.cpp
)

set_target_properties(platform_stress_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin"
)

target_link_libraries(platform_stress_test
    PRIVATE
        camera_subsystem_platform
        camera_subsystem_core
)

# 将压测程序加入 CTest，构建后可自动执行
add_test(NAME platform_stress_test COMMAND platform_stress_test 5)

# FrameBroker 压测程序：不依赖 GTest，始终构建
add_executable(frame_broker_stress_test
    stress/frame_broker_stress_test.cpp
)

set_target_properties(frame_broker_stress_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin"
)

target_link_libraries(frame_broker_stress_test
    PRIVATE
        camera_subsystem_broker
        camera_subsystem_platform
        camera_subsystem_core
)

add_test(NAME frame_broker_stress_test COMMAND frame_broker_stress_test 5)

# CameraSource 压测程序：不依赖 GTest，始终构建
add_executable(camera_source_stress_test
    stress/camera_source_stress_test.cpp
)

set_target_properties(camera_source_stress_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin"
)

target_link_libraries(camera_source_stress_test
    PRIVATE
        camera_subsystem_camera
        camera_subsystem_broker
        camera_subsystem_platform
        camera_subsystem_core
)

add_test(NAME camera_source_stress_test COMMAND camera_source_stress_test 5)

# 根 CMakeLists.txt 负责查找/引入 GTest，这里做目标名自适配
set(GTEST_TARGET "")
set(GTEST_MAIN_TARGET "")

if (TARGET GTest::gtest)
    set(GTEST_TARGET GTest::gtest)
elseif (TARGET GTest::GTest)
    set(GTEST_TARGET GTest::GTest)
endif ()

if (TARGET GTest::gtest_main)
    set(GTEST_MAIN_TARGET GTest::gtest_main)
elseif (TARGET GTest::Main)
    set(GTEST_MAIN_TARGET GTest::Main)
endif ()

if (GTEST_TARGET STREQUAL "" OR GTEST_MAIN_TARGET STREQUAL "")
    message(WARNING "GTest targets not available; skipping unit tests")
    return()
endif ()

# 单元测试
add_executable(test_frame_handle
    unit/test_frame_handle.cpp
)

target_link_libraries(test_frame_handle
    PRIVATE
        camera_subsystem_core
        ${GTEST_TARGET}
        ${GTEST_MAIN_TARGET}
)

add_test(NAME test_frame_handle COMMAND test_frame_handle)

add_executable(test_camera_config
    unit/test_camera_config.cpp
)

target_link_libraries(test_camera_config
    PRIVATE
        camera_subsystem_core
        ${GTEST_TARGET}
        ${GTEST_MAIN_TARGET}
)

add_test(NAME test_camera_config COMMAND test_camera_config)
