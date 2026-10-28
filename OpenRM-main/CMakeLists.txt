cmake_minimum_required(VERSION 3.15)
project(OpenRM)


set(CMAKE_CXX_STANDARD 20)

# set(CMAKE_BUILD_TYPE DEBUG)
# add_compile_options(-g -O0 -w -fno-omit-frame-pointer -Wno-notes)

set(CMAKE_BUILD_TYPE RELEASE)
add_compile_options(-O3 -w)

# OpenCV
find_package(OpenCV 4.5.4 REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS})

# Eigen
find_package(Eigen3 REQUIRED)
include_directories(${EIGEN3_INCLUDE_DIR})

# Ceres
find_package(Ceres REQUIRED)
include_directories(${CERES_INCLUDE_DIRS})

# CUDA
find_package(CUDA)
if (CUDA_FOUND)
    message(STATUS "CUDA Version: ${CUDA_VERSION}")
    include_directories(/usr/local/cuda/include)
    set(OPENRM_WITH_TENSORRT ON)
else()
    message(STATUS "CUDA not found, TensorRT/CUDA modules disabled")
    set(OPENRM_WITH_TENSORRT OFF)
endif()

# AidLite (AidLux / Qualcomm QNN+SNPE NPU 推理后端)
# 注意：不要用 set(... CACHE "") 预置这两个变量，否则 find_path/find_library
# 会因为“缓存里已有值”而跳过搜索。需要手动指定时在 cmake 命令行传入：
#   -DOPENRM_AIDLITE_INCLUDE_DIR=/usr/local/include
#   -DOPENRM_AIDLITE_LIBRARY=/usr/local/lib/libaidlite.so
option(OPENRM_WITH_AIDLITE "Build the AidLite inference backend" ON)

if (OPENRM_WITH_AIDLITE)
    if (NOT OPENRM_AIDLITE_INCLUDE_DIR)
        find_path(
            OPENRM_AIDLITE_INCLUDE_DIR
            NAMES aidlux/aidlite/aidlite.hpp
            PATHS /usr/local/include /usr/include /opt/aidlux/include
        )
    endif()

    if (NOT OPENRM_AIDLITE_LIBRARY)
        find_library(
            OPENRM_AIDLITE_LIBRARY
            NAMES aidlite
            PATHS /usr/local/lib /usr/lib /usr/lib/aarch64-linux-gnu /opt/aidlux/lib
        )
    endif()

    if (OPENRM_AIDLITE_INCLUDE_DIR AND OPENRM_AIDLITE_LIBRARY)
        message(STATUS "AidLite found: ${OPENRM_AIDLITE_LIBRARY}")
        message(STATUS "AidLite include: ${OPENRM_AIDLITE_INCLUDE_DIR}")
        set(OPENRM_WITH_AIDLITE ON)
    else()
        message(STATUS "AidLite NOT found (header=${OPENRM_AIDLITE_INCLUDE_DIR} lib=${OPENRM_AIDLITE_LIBRARY})")
        message(STATUS "  -> install it with: sudo aid-pkg install aidlite-sdk")
        set(OPENRM_WITH_AIDLITE OFF)
    endif()
endif()

# 设置主版本号、次版本号和补丁版本号
set(OPENRM_VERSION_MAJOR 1)
set(OPENRM_VERSION_MINOR 0)
set(OPENRM_VERSION_PATCH 0)

# 生成完整的版本号
set(OPENRM_VERSION ${OPENRM_VERSION_MAJOR}.${OPENRM_VERSION_MINOR}.${OPENRM_VERSION_PATCH})

# 将宏定义添加到项目
add_definitions(-DOPENRM_VERSION="${OPENRM_VERSION}")
# add_definitions(-DPOINTER_IO)

# 将版本号定义为预定义宏，使其在程序内部可访问
configure_file(
    ${CMAKE_SOURCE_DIR}/cmake/VersionConfig.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/VersionConfig.h)


# 添加子目录
add_subdirectory(src/attack)
add_subdirectory(src/kalman)
add_subdirectory(src/pointer)
add_subdirectory(src/solver)
add_subdirectory(src/uniterm)
add_subdirectory(src/utils)
add_subdirectory(src/video)

if (CUDA_FOUND)
    add_subdirectory(src/tensorrt)
    add_subdirectory(cuda)
endif()

# 与后端无关的推理封装（AidLite / TensorRT）
add_subdirectory(src/infer)


# 添加目标文件
if (CUDA_FOUND)
    set(
        TARGETS_LIST
            openrm_attack
            openrm_kalman
            openrm_pointer
            openrm_solver
            openrm_delay
            openrm_print
            openrm_serial
            openrm_tf
            openrm_timer
            openrm_uniterm
            openrm_video
            openrm_tensorrt
            openrm_cudatools
            openrm_infer
    )
else()
    set(
        TARGETS_LIST
            openrm_attack
            openrm_kalman
            openrm_pointer
            openrm_solver
            openrm_delay
            openrm_print
            openrm_serial
            openrm_tf
            openrm_timer
            openrm_uniterm
            openrm_video
            openrm_infer
    )
endif()

# 安装
install(
    TARGETS
        ${TARGETS_LIST}
    EXPORT
        OpenRMTarget
    RUNTIME DESTINATION
        bin
    LIBRARY DESTINATION
        lib
    ARCHIVE DESTINATION
        lib
)

install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/VersionConfig.h
    DESTINATION
        include/openrm
)

if (CUDA_FOUND)
    install(
        FILES
            ${CMAKE_SOURCE_DIR}/cuda/include/cudatools.h
        DESTINATION
            include/openrm
    )
endif()

install(
    DIRECTORY
        ${CMAKE_SOURCE_DIR}/include/
    DESTINATION 
        include/openrm
)

install(
    EXPORT
        OpenRMTarget
    NAMESPACE
        openrm::
    DESTINATION
        lib/cmake/openrm
)

include(CMakePackageConfigHelpers)

if (CUDA_FOUND)
    configure_package_config_file(
        ${CMAKE_SOURCE_DIR}/cmake/OpenRMConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/OpenRMConfig.cmake
        INSTALL_DESTINATION
            lib/cmake/openrm
    )
else()
    configure_package_config_file(
        ${CMAKE_SOURCE_DIR}/cmake/OpenRMConfig.cmake.in.nocuda
        ${CMAKE_CURRENT_BINARY_DIR}/OpenRMConfig.cmake
        INSTALL_DESTINATION
            lib/cmake/openrm
    )
endif()


install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/OpenRMConfig.cmake
    DESTINATION
        lib/cmake/openrm
)

# 卸载
if(NOT TARGET uninstall)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
        IMMEDIATE @ONLY
    )

    add_custom_target(uninstall
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake
    )
endif()

