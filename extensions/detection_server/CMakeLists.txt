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
    src/rknn_model_session.cpp
    src/detection_session.cpp
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

set(_DETECTION_SERVER_OFFICIAL_AI_ROOT
    "${CAMERA_SUBSYSTEM_ROOT}/../official-ai-latest"
)
set(_DETECTION_SERVER_RKNN_INCLUDE_CANDIDATES
    "${_DETECTION_SERVER_OFFICIAL_AI_ROOT}/rknn-toolkit2/rknpu2/runtime/Linux/librknn_api/include"
    "${_DETECTION_SERVER_OFFICIAL_AI_ROOT}/rknn_model_zoo/3rdparty/rknpu2/include"
    "${CAMERA_SUBSYSTEM_ROOT}/../Omni3576-sdk/external/rknpu2/runtime/Linux/librknn_api/include"
)
set(_DETECTION_SERVER_RKNN_LIBRARY_CANDIDATES
    "${_DETECTION_SERVER_OFFICIAL_AI_ROOT}/rknn-toolkit2/rknpu2/runtime/Linux/librknn_api/aarch64/librknnrt.so"
    "${_DETECTION_SERVER_OFFICIAL_AI_ROOT}/rknn_model_zoo/3rdparty/rknpu2/Linux/aarch64/librknnrt.so"
    "${CAMERA_SUBSYSTEM_ROOT}/../Omni3576-sdk/external/rknpu2/runtime/Linux/librknn_api/aarch64/librknnrt.so"
)

set(DETECTION_SERVER_RKNN_INCLUDE_DIR "")
foreach(candidate ${_DETECTION_SERVER_RKNN_INCLUDE_CANDIDATES})
    if (EXISTS "${candidate}/rknn_api.h")
        set(DETECTION_SERVER_RKNN_INCLUDE_DIR "${candidate}")
        break()
    endif ()
endforeach ()

set(DETECTION_SERVER_RKNN_LIBRARY "")
foreach(candidate ${_DETECTION_SERVER_RKNN_LIBRARY_CANDIDATES})
    if (EXISTS "${candidate}")
        set(DETECTION_SERVER_RKNN_LIBRARY "${candidate}")
        break()
    endif ()
endforeach ()

if (CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64" AND
    NOT DETECTION_SERVER_RKNN_INCLUDE_DIR STREQUAL "" AND
    NOT DETECTION_SERVER_RKNN_LIBRARY STREQUAL "")
    target_compile_definitions(camera_detection_server_core PRIVATE DETECTION_SERVER_ENABLE_RKNN_RUNTIME=1)
    target_include_directories(camera_detection_server_core PRIVATE "${DETECTION_SERVER_RKNN_INCLUDE_DIR}")
    target_link_libraries(camera_detection_server_core PUBLIC "${DETECTION_SERVER_RKNN_LIBRARY}")
endif ()

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
