cmake_minimum_required(VERSION 3.12)
project(TJURM-2024)

# AidLux(Ubuntu 24.04) 上：apt 版 OpenCV 的 videoio 依赖 libavutil.so.58，
# 而 libavutil 引用了带版本号的 OpenCL 符号（OPENCL_1.0），系统的 libOpenCL.so.1
# 没有导出符号版本，链接器会报 "undefined reference to clXXX@OPENCL_1.0"。
# 我们并不使用 avutil 的 OpenCL 加速路径，这里放宽"共享库的未定义符号"检查。
if (UNIX AND NOT APPLE)
    add_link_options(-Wl,--allow-shlib-undefined)
endif()

# 设置编译指令
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_BUILD_TYPE DEBUG)
# add_compile_options(-g -O0 -w -fno-omit-frame-pointer -Wno-notes)
add_compile_options(-g)
# set(CMAKE_BUILD_TYPE RELEASE)
# add_compile_options(-O2)

# 设置目标兵种
# add_definitions(-DTJURM_HERO)
add_definitions(-DTJURM_INFANTRY)
# add_definitions(-DTJURM_BALANCE)
# add_definitions(-DTJURM_SENTRY)
# add_definitions(-DTJURM_DRONSE)

# 说明：本工程不再直接依赖 CUDA / TensorRT。
# 推理统一走 OpenRM 的 rm::Infer（后端由 OpenRM 编译期决定：
#   - Nvidia + CUDA  -> TensorRT
#   - AidLux 犀牛派  -> AidLite (Qualcomm QNN, NPU)

# OpenRM
find_package(OpenRM REQUIRED)
include_directories(${OpenRM_INCLUDE_DIRS})

# OpenCV
find_package(OpenCV 4.5.4 REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS})

# Eigen
find_package(Eigen3 REQUIRED)
include_directories(${EIGEN3_INCLUDE_DIR})

# Ceres
find_package(Ceres REQUIRED)
include_directories(${CERES_INCLUDE_DIRS})


# 添加项目头文件
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/libs/json/single_include/nlohmann)


# 添加构建子目录
add_subdirectory(src/data_manager)
add_subdirectory(src/garage)
add_subdirectory(src/threads)


# 添加可执行文件
add_executable(TJURM-2024 src/main.cpp)


# 链接模块静态库
target_link_libraries(TJURM-2024
    data_manager
    garage
    threads
)

install(TARGETS TJURM-2024
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib)
