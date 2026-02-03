cmake_minimum_required(VERSION 3.10)
project(CameraSubsystem VERSION 0.1.0 LANGUAGES CXX)

# 构建选项
option(CAMERA_SUBSYSTEM_BUILD_TESTS "Build unit tests" ON)

# 为 clangd 生成 compile_commands.json
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CAMERA_SUBSYSTEM_WARNING_FLAGS
    -Wall
    -Wextra
    -Werror
)

# 源文件
set(CORE_SOURCES
    src/core/types.cpp
    src/core/frame_handle.cpp
    src/core/camera_config.cpp
    src/core/buffer_pool.cpp
    src/core/buffer_guard.cpp
)

set(PLATFORM_SOURCES
    src/platform/platform_logger.cpp
    src/platform/platform_thread.cpp
    src/platform/platform_epoll.cpp
)

set(UTILS_SOURCES
    src/utils/signal_handler.cpp
)

set(BROKER_SOURCES
    src/broker/frame_broker.cpp
)

set(CAMERA_SOURCES
    src/camera/camera_source.cpp
)

# 创建库
add_library(camera_subsystem_core STATIC
    ${CORE_SOURCES}
)

target_include_directories(camera_subsystem_core
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_compile_options(camera_subsystem_core
    PRIVATE
        ${CAMERA_SUBSYSTEM_WARNING_FLAGS}
)

add_library(camera_subsystem_platform STATIC
    ${PLATFORM_SOURCES}
)

target_include_directories(camera_subsystem_platform
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_compile_options(camera_subsystem_platform
    PRIVATE
        ${CAMERA_SUBSYSTEM_WARNING_FLAGS}
)

add_library(camera_subsystem_broker STATIC
    ${BROKER_SOURCES}
)

target_include_directories(camera_subsystem_broker
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_compile_options(camera_subsystem_broker
    PRIVATE
        ${CAMERA_SUBSYSTEM_WARNING_FLAGS}
)

add_library(camera_subsystem_camera STATIC
    ${CAMERA_SOURCES}
)

target_include_directories(camera_subsystem_camera
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_compile_options(camera_subsystem_camera
    PRIVATE
        ${CAMERA_SUBSYSTEM_WARNING_FLAGS}
)

add_library(camera_subsystem_utils STATIC
    ${UTILS_SOURCES}
)

target_include_directories(camera_subsystem_utils
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_compile_options(camera_subsystem_utils
    PRIVATE
        ${CAMERA_SUBSYSTEM_WARNING_FLAGS}
)

# 链接依赖
target_link_libraries(camera_subsystem_broker
    PUBLIC
        camera_subsystem_core
        camera_subsystem_platform
)

target_link_libraries(camera_subsystem_camera
    PUBLIC
        camera_subsystem_core
        camera_subsystem_platform
)

# 查找依赖
find_package(Threads REQUIRED)

# spdlog: 优先使用系统安装,其次使用 third_party,最后回退到 stub
set(CAMERA_SUBSYSTEM_THIRD_PARTY_DIR "${PROJECT_SOURCE_DIR}/third_party")
set(CAMERA_SUBSYSTEM_SPDLOG_DIR "${CAMERA_SUBSYSTEM_THIRD_PARTY_DIR}/spdlog")
set(CAMERA_SUBSYSTEM_SPDLOG_STUB_DIR "${CAMERA_SUBSYSTEM_THIRD_PARTY_DIR}/spdlog_stub")

set(SPDLOG_TARGET "")

find_package(spdlog CONFIG QUIET)
if (TARGET spdlog::spdlog)
    set(SPDLOG_TARGET spdlog::spdlog)
elseif (TARGET spdlog::spdlog_header_only)
    set(SPDLOG_TARGET spdlog::spdlog_header_only)
endif ()

if (NOT SPDLOG_TARGET AND EXISTS "${CAMERA_SUBSYSTEM_SPDLOG_DIR}/CMakeLists.txt")
    message(STATUS "spdlog not found via find_package; using third_party/spdlog")
    add_subdirectory("${CAMERA_SUBSYSTEM_SPDLOG_DIR}" EXCLUDE_FROM_ALL)

    if (TARGET spdlog::spdlog)
        set(SPDLOG_TARGET spdlog::spdlog)
    elseif (TARGET spdlog::spdlog_header_only)
        set(SPDLOG_TARGET spdlog::spdlog_header_only)
    endif ()
endif ()

if (NOT SPDLOG_TARGET)
    message(WARNING "spdlog not found; falling back to third_party/spdlog_stub")

    add_library(spdlog_stub INTERFACE)
    target_include_directories(spdlog_stub INTERFACE
        "${CAMERA_SUBSYSTEM_SPDLOG_STUB_DIR}/include"
    )

    if (NOT TARGET spdlog::spdlog)
        add_library(spdlog::spdlog ALIAS spdlog_stub)
    endif ()
    if (NOT TARGET spdlog::spdlog_header_only)
        add_library(spdlog::spdlog_header_only ALIAS spdlog_stub)
    endif ()

    set(SPDLOG_TARGET spdlog::spdlog)
endif ()

# 链接外部库
target_link_libraries(camera_subsystem_platform
    PUBLIC
        Threads::Threads
        ${SPDLOG_TARGET}
)

# 安装规则
install(DIRECTORY include/camera_subsystem DESTINATION include)

# 启用测试 (优先系统安装,其次 third_party/googletest,否则跳过)
include(CTest)
if (CAMERA_SUBSYSTEM_BUILD_TESTS AND BUILD_TESTING)
    set(CAMERA_SUBSYSTEM_GTEST_DIR "${CAMERA_SUBSYSTEM_THIRD_PARTY_DIR}/googletest")

    # 先尝试系统安装（Config 模式）
    find_package(GTest CONFIG QUIET)

    # 系统未安装时，回退到 third_party/googletest
    if (NOT GTest_FOUND AND EXISTS "${CAMERA_SUBSYSTEM_GTEST_DIR}/CMakeLists.txt")
        message(STATUS "GTest not found via find_package; using third_party/googletest")

        # 控制 googletest 自身的选项，避免污染全局安装逻辑
        set(BUILD_GMOCK OFF CACHE BOOL "Disable gmock" FORCE)
        set(INSTALL_GTEST OFF CACHE BOOL "Disable gtest install" FORCE)

        add_subdirectory("${CAMERA_SUBSYSTEM_GTEST_DIR}" EXCLUDE_FROM_ALL)
        set(GTest_FOUND TRUE)
    endif ()

    if (GTest_FOUND)
        add_subdirectory(tests)
    else ()
        message(WARNING "GTest not found; skipping unit tests")
    endif ()
endif ()
