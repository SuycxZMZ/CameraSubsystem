cmake_minimum_required(VERSION 3.10)
project(web_preview_gateway VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CAMERA_SUBSYSTEM_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." CACHE PATH
    "CameraSubsystem project root"
)

add_executable(web_preview_gateway
    src/base64.cpp
    src/camera_subscriber_client.cpp
    src/frame_pipeline.cpp
    src/gateway_config.cpp
    src/main.cpp
    src/sha1.cpp
    src/web_server.cpp
)

target_include_directories(web_preview_gateway
    PRIVATE
        include
        "${CAMERA_SUBSYSTEM_ROOT}/include"
)

target_compile_options(web_preview_gateway
    PRIVATE
        -Wall
        -Wextra
        -Werror
)

find_package(Threads REQUIRED)
target_link_libraries(web_preview_gateway PRIVATE Threads::Threads)
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(web_preview_gateway PRIVATE pthread)
endif ()
