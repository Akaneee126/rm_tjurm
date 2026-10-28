# OpenRM-2024



**此算法库为北洋机甲2024视觉算法库**


**适配开源自瞄框架 [TJURM-2024](https://github.com/HHgzs/TJURM-2024)**


## 🧠 推理后端（无 CUDA 也能跑）

从本版本开始，OpenRM 把「推理」抽象成与后端无关的 `rm::Infer`（`include/infer/infer.h`），
编译期自动选择可用后端：

| 平台 | 后端 | 模型文件 | 说明 |
| --- | --- | --- | --- |
| Nvidia Jetson（有 CUDA） | TensorRT | `.engine` / `.onnx` | 原路径不变，`openrm_tensorrt` 自动参与编译 |
| **AidLux 犀牛派 A1/X1（无 CUDA，有 NPU）** | **AidLite + Qualcomm QNN** | `.ctx.bin`（AIMO 转换） | `sudo aid-pkg install aidlite-sdk`，自动探测 |
| 都没有 | 空后端 | — | 编译能过，运行时打印明确错误 |

* CPU 端 letterbox / RGB / 归一化 / NCHW-NHWC 预处理替代了原来的 CUDA `resize` 核函数，几何（等比 + 居中 + 填充 114）与原实现一致，因此 `nms.cpp` 的坐标反算无需改动。
* 工业相机 SDK（大恒 / 迈德威视 / 海康）没装时提供同名空实现（`src/video/stub_camera.cpp`），不会再出现链接失败。
* UVC 相机新增 MJPEG 采集（`openUVCExt`），1280×1024 也能跑高帧率。
* `ncurses` 的 `OK` 宏冲突用 `NCURSES_NOMACROS` 解决，**不再需要修改 `/usr/include/curses.h`**。

完整部署步骤（依赖、AIMO 转模型、Config.json、验证清单）见工作区文档：
[`docs/DEPLOY_AIDLUX.md`](../../docs/DEPLOY_AIDLUX.md)







## 🌟 代码亮点 🌟



### 🎖️ 全阵容

集结**自瞄，打符，打前哨**于一体，适配**步兵，哨兵，英雄，无人机**全体兵种



### 🚀 动态链接库设计

本自瞄算法库采用动态链接库设计，**快速上手，便捷调用**

- 支持cmake 调用 find_package
- 全动态链接库设计

🎉🎉🎉 **使用体验类似 OpenCV** 🎉🎉🎉





### 🙌 算法与框架分离

**算法库**

- 算法库采用动态链接库的形式，方便适配更多视觉项目，便于代码复用

- https://github.com/HHgzs/OpenRM-2024



**自瞄框架**

- 自瞄框架无具体的算法实现，通过调用算法库中实现的算法，搭建流水线架构，实现高效与高拓展性

- https://github.com/HHgzs/TJURM-2024







## 🦺 环境配置 🦺

下面是环境配置的保姆级教程，如有疑问请在讨论区或交流群留言

**注意**：

- 项目使用 **OpenCV4.11.0**，您使用的版本应尽量与项目保持一致

- 可按照下面环境配置流程进行多版本并存安装

**提示**

- **无Nvidia硬件，OpenRM仍可正常编译，tensorrt模块自动不参与编译**
- **无大恒相机驱动，仓库仍可正常编译，工业相机模块不参与编译**



### 基本环境

---

#### 设置系统时区

设置亚洲/上海时区(UTC+8:00)

```bash
sudo timedatectl set-timezone Asia/Shanghai
```

#### 更新系统

更换清华`UbuntuPorts`软件源: [Link](https://mirrors.tuna.tsinghua.edu.cn/help/ubuntu-ports/)

发行版选Ubuntu Jammy 22.04

```bash
# 更新可用软件包列表
sudo apt update

# 更新软件包
sudo apt upgrade

# 安装完成后重启小电脑
sudo reboot
# 如果使用图形桌面访问可以点桌面上的重启
```

#### Cuda、cudnn、TensorRT

由于不同设备属性不一致，请自行安装，本项目所使用**Nvidia NX**及**AGX**系列系统环境自带**CUDA**套装

**无Nvidia硬件，无CUDA，OpenRM仍可正常编译，tensorrt模块不参与编译**

Nvidia Orin Nano Jetpack 6.2安装`nvidia-jetpack`:

```bash
# 更新可用软件包列表
sudo apt update

# 安装nvidia-jetpack
sudo apt install nvidia-jetpack

# 安装完成后重启小电脑
sudo reboot
# 如果使用图形桌面访问可以点桌面上的重启
```

#### 安装Jtop工具

Jtop是一个Jetson的系统监视实用程序，可在终端上运行，可实时查看和控制 NVIDIA Jetson的状态。

安装步骤:
```bash
# 1. 安装pip3工具
sudo apt-get install python3-pip
# 2. 用pip3安装jtop包
sudo -H pip3 install -U jetson-stats
# 3. 重启后可运行
jtop
```

---

#### gcc/g++

本项目中使用的 `gcc/g++` 版本为 `8.4.0`，请确保您的  `gcc/g++` 版本能够正常编译本项目

---



#### cmake

本项目使用的 `cmake` 版本为 `3.22.1`，若本机 `cmake` 版本低于 `3.12` 需要更新 `cmake` 版本

查看 `cmake` 版本与 `gcc/g++` 之间的适配关系：

[CXX_STANDARD](https://cmake.org/cmake/help/v3.12/prop_tgt/CXX_STANDARD.html)


首先下载需要版本的 `cmake`

[cmake](https://cmake.org/files/)

本项目中下载的是 `cmake-3.22.1.tar.gz`，


解压后进入文件夹

```bash
tar -xzvf  cmake-3.22.1.tar.gz
cd cmake-3.22.1
```

如果没有bin目录就编译安装

```bash
./bootstrap
make -j6
sudo make install
```

如果有bin目录就可以直接重命名文件夹后拷贝到软件目录即可

```bash
# 返回上级目录
cd ..
mv cmake-3.22.1 cmake
sudo cp -r ./cmake /usr/local
```

修改环境变量

```bash
vim ~/.bashrc
```

在文件最后添加 `export PATH=/usr/local/cmake/bin:$PATH`

```bash
source ~/.bashrc
cmake --version
```

此时应该能够正确打印版本信息，如果后续还是无法使用cmake，则需要加一步软链接

```bash
sudo ln -s /usr/local/cmake/bin/cmake /usr/local/bin/camke
```

---



#### Eigen

可以使用apt安装

```bash
sudo apt install libeigen3-dev
```

也可以选择源码编译
[Eigen](http://eigen.tuxfamily.org/index.php?title=Main_Page)
[eigen-git-mirror](https://github.com/eigenteam/eigen-git-mirror)


编译安装

```bash
cd eigen-git-mirror
mkdir build
cd build
cmake ..
sudo make install
```

---





#### Ceres

首先安装必要依赖

```bash
sudo apt-get install liblapack-dev libsuitesparse-dev libcxsparse3 libgflags-dev libgoogle-glog-dev libgtest-dev
```

设置CUDA可执行文件环境变量

```bash
# 添加到bashrc里
echo 'export PATH=/usr/local/cuda/bin:$PATH' >> ~/.bashrc

# 重新加载环境变量
source ~/.bashrc
```

从Github上获取ceres源码
[ceres-solver](https://github.com/ceres-solver/ceres-solver)

这里使用的是`2.2.0`版本，下载tar.gz压缩包

解压缩并编译

```bash
tar -zxvf ceres-solver-2.2.0.tar.gz 
cd ceres-solver-2.2.0/
mkdir build
cd build/

cmake ..
make -j6
sudo make install
```

---





#### Ncurses

一条指令安装

```bash
sudo apt-get install libncurses5-dev libncursesw5-dev
```

注意由于ncueses中的宏定义 `OK`与 `OpenCV` 中冲突，所以需要对其进行修改

首先修改权限

```bash
sudo chmod 777 /usr/include/curses.h
```

使用VSCode全局替换，将 `OK` 修改为 `KO`

最后将权限改回来

```bash
sudo chmod 644 /usr/include/curses.h
```

---





#### OpenCV 4.5.4 多版本并存

首先安装必要依赖

```bash
sudo apt-get update
sudo apt-get install build-essential cmake pkg-config 
sudo apt-get install libgtk2.0-dev libavcodec-dev libavformat-dev libswscale-dev
sudo apt-get install python-dev-is-python3 libtbb2 libtbb-dev libjpeg-dev libpng-dev libtiff-dev
```

**依赖如若不完整，请自行上网搜索**



分别前往 `OpenCV` 和 `opencv_contrib` 的 github 仓库下载源码

注意 OpenCV 和 opencv_contrib 的版本要对应正确

- [opencv](https://github.com/opencv/opencv/releases)
- [opencv_contrib](https://github.com/opencv/opencv_contrib/tags)


将 `opencv-4.5.4.zip` 和 `opencv_contrib-4.5.4.zip` 放在同一文件夹，分别解压缩

```bash
unzip opencv-4.11.0.zip
unzip opencv_contrib-4.11.0.zip
cd opencv-4.11.0
mkdir build
cd build
```

在/usr/local/下新建一个文件夹，用于存放opencv的版本

```bash
mkdir /usr/local/opencv-4.11.0
```

如果选择安装opencv的cuda功能和opencv拓展包，可以使用下面的命令

```bash
cmake \
-DCMAKE_BUILD_TYPE=Release \
-DCMAKE_INSTALL_PREFIX=/usr/local/opencv-4.11.0 \
-DOPENCV_ENABLE_NONFREE=1 \
-DBUILD_opencv_python3=1 \
-DWITH_FFMPEG=1 \
-DCUDA_TOOLKIT_ROOT_DIR=/usr/local/cuda \
-DCUDA_ARCH_BIN=8.7 \
-DCUDA_ARCH_PTX=8.7 \
-DWITH_CUDA=1 \
-DENABLE_FAST_MATH=1 \
-DCUDA_FAST_MATH=1 \
-DWITH_CUBLAS=1 \
-DOPENCV_GENERATE_PKGCONFIG=1 \
-D BUILD_opencv_sfm=OFF \
-DOPENCV_DOWNLOAD_MIRROR_ID=gitcode \
-DOPENCV_EXTRA_MODULES_PATH=../../opencv_contrib-4.11.0/modules ..
```



**CUDA_ARCH**版本应与自己的**CUDA**一致，这里仅介绍带 CUDA 的 OpenCV 安装方法，不带 CUDA 的安装同理



接着执行编译安装

```bash
make -j6
sudo make install
```

---





#### 大恒驱动

前往大恒官网下载c++驱动

https://www.daheng-imaging.com/downloads/

本项目中使用的是 【大恒相机】【USB3.0】【ARM Linux】

**Galaxy Linux-armhf-Gige-U3 SDK**

#### 迈德威视驱动

前往迈德威视官网下载最新LinuxSDK

https://www.mindvision.com.cn/category/software/

解压后运行`sudo install.sh`

完成后重启小电脑

如不安装相机驱动，仓库仍可正常编译

---







### OpenRM 安装

使用 `run.sh` 自动安装

```bash
cd OpenRM-2024
sudo ./run.sh -t
```

> 这里有坑：有时会出现找不到cudart和cublas的情况，自己把这两个软链接到/usr/local/lib后方可编译通过
例如在Jetpak6.1上：

```bash
# 出现如下两个错误：
# /usr/bin/ld: cannot find -lcudart: No such file or directory
# /usr/bin/ld: cannot find -lcublas: No such file or directory
sudo ln -s /usr/local/cuda/targets/aarch64-linux/lib/libcublas.so /usr/local/lib/libcublas.so
sudo ln -s /usr/local/cuda/targets/aarch64-linux/lib/libcudart.so /usr/local/lib/libcudart.so
```

`run.sh` 有多种功能：

- **-t** 编译安装**OpenRM**动态链接库后，编译安装名为 **openrm** 的参数面板程序
- **-r** 删除编译和安装结果，并重新编译
- **-d** 彻底删除 OpenRM
- **-i** 重新安装
- **-g \<arg>** 调用git，需添加commit
- 不添加参数，只编译安装 **OpenRM** 动态链接库





## 🧩 模块介绍 🧩



### cudatools

cuda编程部分，调用**NVCC**编译，目前实现了图像resize功能

```c++
void rm::resize(
    uint8_t* src,
    int src_width,
    int src_height,
    float* dst,
    int dst_width,
    int dst_height,
    void* cuda_stream
);
```



---



### tensorrt

调用tensorrt加速推理，以及yolo系的nms算法

```c++
bool rm::initTrtOnnx(
    const std::string& onnx_file,
    const std::string& engine_file,
    nvinfer1::IExecutionContext** context,
    unsigned int batch_size = 1U
);

bool rm::initTrtEngine(
    const std::string& engine_file,
    nvinfer1::IExecutionContext** context
);

bool rm::initCudaStream(
    cudaStream_t* stream
);

void rm::detectEnqueue(
    float* input_device_buffer,
    float* output_device_buffer,
    nvinfer1::IExecutionContext** context,
    cudaStream_t* stream
);

void rm::detectOutput(
    float* output_host_buffer,
    const float* output_device_buffer,
    cudaStream_t* stream,
    size_t output_struct_size,
    int bboxes_num,
    int batch_size = 1
); 

void rm::detectOutputClassify(
    float* output_host_buffer,
    const float* output_device_buffer,
    cudaStream_t* stream,
    int class_num
); 

void rm::mallocYoloCameraBuffer(
    uint8_t** rgb_host_buffer,
    uint8_t** rgb_device_buffer,
    int rgb_width,
    int rgb_height,
    int batch_size = 1,
    int channels = 3
);


void rm::mallocYoloDetectBuffer(
    float** input_device_buffer,
    float** output_device_buffer,
    float** output_host_buffer,
    int input_width,
    int input_height,
    size_t output_struct_size,
    int bboxes_num,
    int batch_size = 1,
    int channels = 3
);

void rm::mallocClassifyBuffer(
    float** input_host_buffer,
    float** input_device_buffer,
    float** output_device_buffer,
    float** output_host_buffer,
    int input_width,
    int input_height,
    int class_num,
    int channels = 3
);

void rm::freeYoloCameraBuffer(
    uint8_t* rgb_host_buffer,
    uint8_t* rgb_device_buffer
);


void rm::freeYoloDetectBuffer(
    float* input_device_buffer,
    float* output_device_buffer,
    float* output_host_buffer
);

void rm::freeClassifyBuffer(
    float* input_host_buffer,
    float* input_device_buffer,
    float* output_device_buffer,
    float* output_host_buffer
);

void rm::memcpyYoloCameraBuffer(
    uint8_t* rgb_mat_data,
    uint8_t* rgb_host_buffer,
    uint8_t* rgb_device_buffer,
    int rgb_width,
    int rgb_height,
    int channels = 3
);

void rm::memcpyClassifyBuffer(
    uint8_t* mat_data,
    float* input_host_buffer,
    float* input_device_buffer,
    int input_width,
    int input_height,
    int channels = 3
);

std::vector<YoloRect> rm::yoloArmorNMS_V5C36(
    float* output_host_buffer,
    int output_bboxes_num,
    int armor_classes_num,
    float confidence_threshold,
    float nms_threshold,
    int input_width,
    int input_height,
    int infer_width,
    int infer_height
);

std::vector<YoloRect> rm::yoloArmorNMS_V5(
    float* output_host_buffer,
    int output_bboxes_num,
    int armor_classes_num,
    float confidence_threshold,
    float nms_threshold,
    int input_width,
    int input_height,
    int infer_width,
    int infer_height
);

std::vector<YoloRect> rm::yoloArmorNMS_FP(
    float* output_host_buffer,
    int output_bboxes_num,
    int classes_num,
    float confidence_threshold,
    float nms_threshold,
    int input_width,
    int input_height,
    int infer_width,
    int infer_height
);

std::vector<YoloRect> rm::yoloArmorNMS_FPX(
    float* output_host_buffer,
    int output_bboxes_num,
    int classes_num,
    float confidence_threshold,
    float nms_threshold,
    int input_width,
    int input_height,
    int infer_width,
    int infer_height
);
```

---



### attach

攻击目标选择及切换模块

```c++
class AttackInterface;
```

```c++
bool rm::isValidArmorID(ArmorID armor_id, char valid_byte);
double rm::getAngleOffsetTargetToReferee(
    const double yaw,
    const double pitch,
    const double target_x,
    const double target_y,
    const double target_z,
    const double referee_x,
    const double referee_y,
    const double referee_z,
    const double referee_yaw = 0.0,
    const double referee_pitch = 0.0,
    const double axis_x = 0.0,
    const double axis_y = 0.0,
    const double axis_z = 0.0);
```



### kalman

基于KF和EKF的运动预测模型

```c++
class rm::AntitopV1;
class rm::AntitopV2;
class rm::AntitopV3;
```

```c++
class rm::OutpostV1;
class rm::OutpostV2;
```

```c++
class rm::RuneV1;
class rm::RuneV2;
```


```c++
class rm::TQstateV1;
class rm::TQstateV2;
class rm::TQstateV3;
class rm::TQstateV4;
```

```c++
class rm::trajectoryV1;
```



### uniterm

用于openrm参数面板程序的库，可被调用添加LOG

```c++
void rm::message(const std::string& name, int msg);
void rm::message(const std::string& name, float msg);
void rm::message(const std::string& name, double msg);
void rm::message(const std::string& name, char msg);
void rm::message(const std::string& name, MsgNum msg);
void rm::message(const std::string& msg, MSG type = MSG_NOTE);
void rm::message(const std::string& info, int img_width, int img_height, cv::Rect rect);
void rm::message(const std::string& info, int img_width, int img_height, std::vector<cv::Point2f> four_points);
void rm::message(const std::string& info, int img_width, int img_height, cv::Point2f pointX);
```



### pointer

用于传统视觉查找灯条和四点定位的模块



### solver

实现了上海交通大学交龙战队提出的基于三分法的pnp解算



### video

摄像头驱动接口，支持UVC相机和大恒相机



### serial

串口通讯，支持usb转ttl，以及虚拟串口



### tf

坐标转换，对自瞄中使用的坐标转换进行了具体实现



### delay

飞行延迟计算



### print

调参打印工具



### timer

时间模块









## 💡 扩展方法 💡




一共需要修改五处，下面以添加`openrm_timer`为例，其源码为`src/utils/timer.cpp`

`include/openrm.h`

```c++
#include "utils/timer.h"
```

`src/utils/CMakeLists.txt`

```cmake
add_library(
    openrm_timer
        SHARED
)
target_sources(
    openrm_timer
        PRIVATE
        ${CMAKE_SOURCE_DIR}/src/utils/timer.cpp
)
target_include_directories(
    openrm_timer
        PRIVATE
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include/openrm>
)
```


`CMakeLists.txt`

```cmake
add_subdirectory(src/utils)
```


`CMakeLists.txt`

```cmake
if (CUDA_FOUND)
    set(
        TARGETS_LIST
            openrm_timer
    )
else()
    set(
        TARGETS_LIST
            openrm_timer
    )
endif()
```

`cmake/OpenRMCopfig.cmake.in`

```cmake
set(
    OPENRM_LIBS
        openrm::openrm_timer
)
```

`cmake/OpenRMConfig.cmake.in.nocuda`

```cmake
set(
    OPENRM_LIBS
        openrm::openrm_timer
)
```



