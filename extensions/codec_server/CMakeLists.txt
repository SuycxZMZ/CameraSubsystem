cmake_minimum_required(VERSION 3.10)

if (NOT DEFINED CAMERA_SUBSYSTEM_ROOT)
    set(CAMERA_SUBSYSTEM_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." CACHE PATH
        "CameraSubsystem project root"
    )
endif ()

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(camera_codec_server VERSION 0.1.0 LANGUAGES CXX)

    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)

    if (NOT DEFINED CAMERA_SUBSYSTEM_RUNTIME_OUTPUT_DIR)
        set(CAMERA_SUBSYSTEM_RUNTIME_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}")
    endif ()
endif ()

add_executable(camera_codec_server
    src/camera_stream_subscriber.cpp
    src/codec_server_app.cpp
    src/codec_server_config.cpp
    src/codec_control_protocol.cpp
    src/codec_control_server.cpp
    src/h264_mpp_encoder.cpp
    src/jpeg_decode_stage.cpp
    src/recording_file_writer.cpp
    src/recording_session_manager.cpp
    src/main.cpp
)

set(_CODEC_SERVER_MPP_INCLUDE_DIR
    "${CAMERA_SUBSYSTEM_ROOT}/../Omni3576-sdk/external/mpp/inc"
)
set(_CODEC_SERVER_MPP_LIBRARY
    "${CAMERA_SUBSYSTEM_ROOT}/../Omni3576-sdk/external/rknpu2/examples/3rdparty/mpp/Linux/aarch64/librockchip_mpp.so"
)
if (DEFINED OMNI3576_SDK_ROOT)
    set(_CODEC_SERVER_MPP_INCLUDE_DIR "${OMNI3576_SDK_ROOT}/external/mpp/inc")
    set(_CODEC_SERVER_MPP_LIBRARY "${OMNI3576_SDK_ROOT}/external/rknpu2/examples/3rdparty/mpp/Linux/aarch64/librockchip_mpp.so")
endif ()

if (CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64"
    AND EXISTS "${_CODEC_SERVER_MPP_INCLUDE_DIR}/mpp_buffer.h"
    AND EXISTS "${_CODEC_SERVER_MPP_LIBRARY}")
    set(CODEC_SERVER_ENABLE_MPP_JPEG_DECODE ON)
else ()
    set(CODEC_SERVER_ENABLE_MPP_JPEG_DECODE OFF)
endif ()

set_target_properties(camera_codec_server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CAMERA_SUBSYSTEM_RUNTIME_OUTPUT_DIR}"
)

target_include_directories(camera_codec_server
    PRIVATE
        include
        "${CAMERA_SUBSYSTEM_ROOT}/include"
)

if (CODEC_SERVER_ENABLE_MPP_JPEG_DECODE)
    target_compile_definitions(camera_codec_server PRIVATE CODEC_SERVER_ENABLE_MPP_JPEG_DECODE=1)
    target_include_directories(camera_codec_server PRIVATE "${_CODEC_SERVER_MPP_INCLUDE_DIR}")
    target_link_libraries(camera_codec_server PRIVATE "${_CODEC_SERVER_MPP_LIBRARY}")
endif ()

target_compile_options(camera_codec_server
    PRIVATE
        -Wall
        -Wextra
        -Werror
)

find_package(Threads REQUIRED)
target_link_libraries(camera_codec_server PRIVATE Threads::Threads)
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(camera_codec_server PRIVATE pthread)
endif ()

# RecordingFileWriter verification tool
add_executable(recording_file_writer_test
    src/recording_file_writer.cpp
    src/recording_file_writer_test.cpp
)

set_target_properties(recording_file_writer_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CAMERA_SUBSYSTEM_RUNTIME_OUTPUT_DIR}"
)

target_include_directories(recording_file_writer_test
    PRIVATE
        include
        "${CAMERA_SUBSYSTEM_ROOT}/include"
)

target_compile_options(recording_file_writer_test
    PRIVATE
        -Wall
        -Wextra
        -Werror
)

target_link_libraries(recording_file_writer_test PRIVATE Threads::Threads)
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(recording_file_writer_test PRIVATE pthread)
endif ()

# RecordingSessionManager verification tool
add_executable(recording_session_manager_test
    src/camera_stream_subscriber.cpp
    src/codec_control_protocol.cpp
    src/h264_mpp_encoder.cpp
    src/jpeg_decode_stage.cpp
    src/recording_file_writer.cpp
    src/recording_session_manager.cpp
    src/recording_session_manager_test.cpp
)

set_target_properties(recording_session_manager_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CAMERA_SUBSYSTEM_RUNTIME_OUTPUT_DIR}"
)

target_include_directories(recording_session_manager_test
    PRIVATE
        include
        "${CAMERA_SUBSYSTEM_ROOT}/include"
)

if (CODEC_SERVER_ENABLE_MPP_JPEG_DECODE)
    target_compile_definitions(recording_session_manager_test PRIVATE CODEC_SERVER_ENABLE_MPP_JPEG_DECODE=1)
    target_include_directories(recording_session_manager_test PRIVATE "${_CODEC_SERVER_MPP_INCLUDE_DIR}")
    target_link_libraries(recording_session_manager_test PRIVATE "${_CODEC_SERVER_MPP_LIBRARY}")
endif ()

target_compile_options(recording_session_manager_test
    PRIVATE
        -Wall
        -Wextra
        -Werror
)

target_link_libraries(recording_session_manager_test PRIVATE Threads::Threads)
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(recording_session_manager_test PRIVATE pthread)
endif ()

# JpegDecodeStage verification tool
add_executable(jpeg_decode_stage_test
    src/jpeg_decode_stage.cpp
    src/jpeg_decode_stage_test.cpp
)

set_target_properties(jpeg_decode_stage_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CAMERA_SUBSYSTEM_RUNTIME_OUTPUT_DIR}"
)

target_include_directories(jpeg_decode_stage_test
    PRIVATE
        include
        "${CAMERA_SUBSYSTEM_ROOT}/include"
)

if (CODEC_SERVER_ENABLE_MPP_JPEG_DECODE)
    target_compile_definitions(jpeg_decode_stage_test PRIVATE CODEC_SERVER_ENABLE_MPP_JPEG_DECODE=1)
    target_include_directories(jpeg_decode_stage_test PRIVATE "${_CODEC_SERVER_MPP_INCLUDE_DIR}")
    target_link_libraries(jpeg_decode_stage_test PRIVATE "${_CODEC_SERVER_MPP_LIBRARY}")
endif ()

target_compile_options(jpeg_decode_stage_test
    PRIVATE
        -Wall
        -Wextra
        -Werror
)

# H264MppEncoder verification tool
add_executable(h264_mpp_encoder_test
    src/h264_mpp_encoder.cpp
    src/h264_mpp_encoder_test.cpp
)

set_target_properties(h264_mpp_encoder_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CAMERA_SUBSYSTEM_RUNTIME_OUTPUT_DIR}"
)

target_include_directories(h264_mpp_encoder_test
    PRIVATE
        include
        "${CAMERA_SUBSYSTEM_ROOT}/include"
)

if (CODEC_SERVER_ENABLE_MPP_JPEG_DECODE)
    target_compile_definitions(h264_mpp_encoder_test PRIVATE CODEC_SERVER_ENABLE_MPP_JPEG_DECODE=1)
    target_include_directories(h264_mpp_encoder_test PRIVATE "${_CODEC_SERVER_MPP_INCLUDE_DIR}")
    target_link_libraries(h264_mpp_encoder_test PRIVATE "${_CODEC_SERVER_MPP_LIBRARY}")
endif ()

target_compile_options(h264_mpp_encoder_test
    PRIVATE
        -Wall
        -Wextra
        -Werror
)
