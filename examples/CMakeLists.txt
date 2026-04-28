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

add_executable(mplane_dmabuf_probe
    mplane_dmabuf_probe.cpp
)

set_target_properties(mplane_dmabuf_probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CAMERA_SUBSYSTEM_RUNTIME_OUTPUT_DIR}"
)

set(_CAMERA_SUBSYSTEM_RGA_ROOT
    "${OMNI3576_SDK_ROOT}/external/rknpu2/examples/3rdparty/rga"
)
set(_CAMERA_SUBSYSTEM_RGA_INCLUDE_DIR
    "${_CAMERA_SUBSYSTEM_RGA_ROOT}/include"
)
set(_CAMERA_SUBSYSTEM_RGA_LIBRARY
    "${_CAMERA_SUBSYSTEM_RGA_ROOT}/libs/Linux/gcc-aarch64/librga.so"
)

if (CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64"
    AND EXISTS "${_CAMERA_SUBSYSTEM_RGA_INCLUDE_DIR}/im2d.h"
    AND EXISTS "${_CAMERA_SUBSYSTEM_RGA_LIBRARY}")
    add_executable(rga_dmabuf_import_probe
        rga_dmabuf_import_probe.cpp
    )

    set_target_properties(rga_dmabuf_import_probe PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CAMERA_SUBSYSTEM_RUNTIME_OUTPUT_DIR}"
    )

    target_include_directories(rga_dmabuf_import_probe
        PRIVATE
            "${_CAMERA_SUBSYSTEM_RGA_INCLUDE_DIR}"
    )

    target_link_libraries(rga_dmabuf_import_probe
        PRIVATE
            "${_CAMERA_SUBSYSTEM_RGA_LIBRARY}"
    )
else ()
    message(STATUS "RGA DMA-BUF import probe disabled: RK3576 RGA SDK headers/libs not found")
endif ()

set(_CAMERA_SUBSYSTEM_MPP_INCLUDE_DIR
    "${OMNI3576_SDK_ROOT}/external/mpp/inc"
)
set(_CAMERA_SUBSYSTEM_MPP_LIBRARY
    "${OMNI3576_SDK_ROOT}/external/rknpu2/examples/3rdparty/mpp/Linux/aarch64/librockchip_mpp.so"
)

if (CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64"
    AND EXISTS "${_CAMERA_SUBSYSTEM_MPP_INCLUDE_DIR}/mpp_buffer.h"
    AND EXISTS "${_CAMERA_SUBSYSTEM_MPP_LIBRARY}")
    add_executable(mpp_dmabuf_import_probe
        mpp_dmabuf_import_probe.cpp
    )

    set_target_properties(mpp_dmabuf_import_probe PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CAMERA_SUBSYSTEM_RUNTIME_OUTPUT_DIR}"
    )

    target_include_directories(mpp_dmabuf_import_probe
        PRIVATE
            "${_CAMERA_SUBSYSTEM_MPP_INCLUDE_DIR}"
    )

    target_link_libraries(mpp_dmabuf_import_probe
        PRIVATE
            "${_CAMERA_SUBSYSTEM_MPP_LIBRARY}"
    )

    add_executable(mpp_jpeg_decode_probe
        mpp_jpeg_decode_probe.cpp
    )

    set_target_properties(mpp_jpeg_decode_probe PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CAMERA_SUBSYSTEM_RUNTIME_OUTPUT_DIR}"
    )

    target_include_directories(mpp_jpeg_decode_probe
        PRIVATE
            "${_CAMERA_SUBSYSTEM_MPP_INCLUDE_DIR}"
    )

    target_link_libraries(mpp_jpeg_decode_probe
        PRIVATE
            "${_CAMERA_SUBSYSTEM_MPP_LIBRARY}"
    )
else ()
    message(STATUS "MPP DMA-BUF import probe disabled: RK3576 MPP SDK headers/libs not found")
endif ()
