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
