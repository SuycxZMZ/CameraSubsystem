cmake_minimum_required(VERSION 3.10)

if (NOT DEFINED CAMERA_SUBSYSTEM_ROOT)
    set(CAMERA_SUBSYSTEM_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." CACHE PATH
        "CameraSubsystem project root"
    )
endif ()

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(camera_detection_server VERSION 0.1.0 LANGUAGES CXX)

    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)

    if (NOT DEFINED CAMERA_SUBSYSTEM_RUNTIME_OUTPUT_DIR)
        set(CAMERA_SUBSYSTEM_RUNTIME_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}")
    endif ()
endif ()

add_library(camera_detection_server_core STATIC
    src/detection_types.cpp
    src/detection_config.cpp
    src/performance_profile_manager.cpp
    src/camera_detection_server_app.cpp
)

set_target_properties(camera_detection_server_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

target_include_directories(camera_detection_server_core
    PUBLIC
        include
        "${CAMERA_SUBSYSTEM_ROOT}/include"
)

target_compile_options(camera_detection_server_core
    PRIVATE
        -Wall
        -Wextra
        -Werror
)

target_link_libraries(camera_detection_server_core
    PUBLIC
        camera_subsystem_platform
        camera_subsystem_core
)

add_executable(camera_detection_server
    src/main.cpp
)

set_target_properties(camera_detection_server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CAMERA_SUBSYSTEM_RUNTIME_OUTPUT_DIR}"
)

target_include_directories(camera_detection_server
    PRIVATE
        include
        "${CAMERA_SUBSYSTEM_ROOT}/include"
)

target_compile_options(camera_detection_server
    PRIVATE
        -Wall
        -Wextra
        -Werror
)

target_link_libraries(camera_detection_server
    PRIVATE
        camera_detection_server_core
)
