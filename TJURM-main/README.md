# TJURM-2024

**北洋机甲2024自瞄框架**



---

**本框架需配合视觉算法库 [OpenRM](https://github.com/HHgzs/OpenRM-2024) 食用**


## 🧠 在 AidLux 犀牛派 A1 / X1（无 CUDA，有 NPU）上运行

本版本已**移除对 CUDA/TensorRT 的硬依赖**：推理统一走 OpenRM 的 `rm::Infer`，
Jetson 上仍是 TensorRT，AidLux 犀牛派 A1(QCS6490)/X1(QCS8550) 上换成 **AidLite + Qualcomm QNN**
（模型由官方 AIMO 平台把 ONNX 转成 `.ctx.bin`，跑在 Hexagon NPU 上）。

* `CMakeLists.txt` 不再 `find_package(CUDA REQUIRED)`
* `pipeline.h` 的 `cudaStream_t / nvinfer1::IExecutionContext*` → `std::shared_ptr<rm::Infer>`
* 三条流水线（baseline / fourpoints / rune）的预处理器负责 letterbox + 入队，检测器负责取结果 + NMS
* 相机新增 UVC 驱动（支持 MJPEG），`Config.json` 增加 `Camera.Driver` 与 `Model.*.DirModel/InputLayout/...`
* 首次部署建议 `./run.sh -b` 只编译，并把 `Debug.Control.Serial/Timeout` 置 `false` 先验证视觉

依赖安装、AIMO 转模型、参数解释、性能预估与风险清单见工作区文档：
[`docs/DEPLOY_AIDLUX.md`](../../docs/DEPLOY_AIDLUX.md)





## 🌟 代码亮点 🌟



### 🎖️ 全阵容

集结**自瞄，打符，打前哨**于一体，适配**步兵，哨兵，英雄，无人机**全体兵种





### 🚀 流水线架构

本自瞄系统采用流水线架构，可在 GPU 耗时较高的情况下保证程序运行吞吐量

主流水线推理 **v5n** 网络，在 **Nvidia NX Xavier** 设备上可达 **180fps**，在 **Nvidia AGX Orin** 设备上可达 **300fps**





### 🙌 算法与框架分离



**算法库**

- 算法库采用动态链接库的形式，方便适配更多视觉项目，便于代码复用

- https://github.com/HHgzs/OpenRM-2024



**自瞄框架**

- 自瞄框架无具体的算法实现，通过调用算法库中实现的算法，搭建流水线架构，实现高效与高拓展性

- https://github.com/HHgzs/TJURM-2024





### 🔌 易拓展模型

支持大多数队伍使用的神经网络输出格式，可支持任意尺寸的模型输入，仅需通过修改参数实现



**V5**

- 标准的 yolov5 输出格式，即  **(x + y + w + h) + conf + n * classes**
- 其中的类别项可包含颜色也可不包含颜色

**FP**

- 四点格式，即 **(x0 + y0 + x1 + y1 + x2 + y2 + x3 + y3) + conf + n * classes**
- 其中的类别项可包含颜色也可不包含颜色

**FPX**

- 四点格式，颜色与 ID 分离
- **(x0 + y0 + x1 + y1 + x2 + y2 + x3 + y3) + conf + m * color_classes + n * id_classes**





## 🧬 基本特性 🧬

- 算力设备：**Nvidia NX Xavier**
- 工业相机：**大恒 USB3.0**
- 推荐分辨率：**1280x1024**
- 推荐镜头：**6mm（cmos 1/2.9）**





## 🦺 环境配置 🦺



### 编译安装 OpenRM

首先需要配置 OpenRM 库，详见仓库 README，请访问 OpenRM-2024 仓库页面，记得点个星星 ⭐️

https://github.com/HHgzs/OpenRM-2024





### 编译安装 TJURM-2024

成功编译和安装 OpenRM 动态链接库后，TJURM-2024 的环境依赖基本安装完毕了



运行 `run.sh` 自动执行编译和安装 注意第一次編譯會將onnx轉換為engine模型,需將TJURM/config/Config.json中control下的serial及timeout置false.

```bash
cd TJURM-2024
sudo ./run.sh
```



安装完成后，将显示 `<--- Run Code --->`，代码正式开始运行

```bash
[ 93%] Linking CXX static library libthreads.a
[ 93%] Built target threads
[ 96%] Building CXX object CMakeFiles/TJURM-2024.dir/src/main.cpp.o
[100%] Linking CXX executable TJURM-2024
[100%] Built target TJURM-2024

<--- Total Lines --->
        4110

<--- Run Code --->
```



由于本自瞄仓库为所有兵种共用，为避免参数等修改导致不同兵种间冲突，且为避免仓库频繁更新

 `TJURM-2024/data/uniconfig` 下的参数信息将被安装到 `/etc/openrm` ，并建立名为 `TJURM-2024/config` 的软链接到 `TJURM-2024` 代码地址下

此时通过访问并修改 `TJURM-2024/config` 内的参数便可直接更改系统中的参数，在下次代码运行时直接调用



### 自启动


複製配置文件
```bash
sudo cp ./data/uniconfig/tjurm.service  /lib/systemd/system/
```


 重新載入 systemd
 ```bash
sudo systemctl daemon-reload
```
 啟用開機自啟
 ```bash
sudo systemctl enable tjurm.service
```
禁止系統休眠
```bash
sudo systemctl mask sleep.target suspend.target hibernate.target hybrid-sleep.target
```


### Ubuntu 虚拟显示器
```bash
# 首先安装相关软件包
sudo apt-get install xserver-xorg-video-dummy

# 复制虚拟显示器xorg配置文件
sudo cp ~/Workspace/dummy_xorg.conf /usr/share/X11/xorg.conf.d/xorg.conf
```

### 图像输出

如果想要开启 `imshow` 功能，需在运行时加上 `-s` 指令。



- 可在项目文件夹 `TJURM-2024` 内运行 `sudo ./run.sh -s`

  - 该方法将调用 `guard.sh` 守护脚本，保证代码在运行中终止时立即重启


- 可在任意地址下运行 `TJURM-2024 -s`

  - 由于程序已安装在 `\usr\local\bin`  该方法全局有效
  - 该方法仅调用一次运行，不开启守护脚本


### 录像有坑

https://forums.developer.nvidia.com/t/open-cheese-failed-in-jetpack-6-0/304314/3



### 参数显示

在 OpenRM 自瞄算法库中，实现了一个基于 `ncurses` 的终端参数面板，详见 **OpenRM-2024** 仓库

https://github.com/HHgzs/OpenRM-2024



#### 开启参数面板

```bash
openrm -d
```



**💥注意**

- 如果没有 openrm 程序，请检查 OpenRM 安装中是否忽略了安装此参数面板程序的步骤
- 详见 **OpenRM-2024** 的 `README`   https://github.com/HHgzs/OpenRM-2024





#### 开启示波器

在参数面板左侧，有参数名称和参数值的元组，如果需要将某个或某几个参数以示波器的形式展示出来，可以使用 `openrm` 提供的示波器功能



例如对于参数 `target omg`，对于有空格的参数，可以使用转义字符

```bash
openrm -o target\ omg
```

或者使用引号

```bash
openrm -o "target omg"
```



最多可支持四个波形同时显示

```bash
openrm -o "target -x-" "target -y-" "target -z-" "target omg"
```











## 📚️ 文件介绍 📚️

```bash
.
├── CMakeLists.txt                   # CMake文件
├── config -> /etc/openrm            # 用于修改参数的软链接
├── data
│   ├── debug                        # debug功能，逐帧存储图像
│   ├── speed                        # 弹速记录文件存储位置
│   ├── uniconfig                    # 统一参数，可通过 sudo ./run.sh -c 强制覆盖系统参数
│   │   ├── CamLens.json             # 相机参数
│   │   ├── Config.json              # 主参数，下文将详细介绍
│   │   ├── guard.sh                 # 守护脚本
│   │   ├── image                    # 重投影蒙版
│   │   └── models                   # onnx模型放在这里
│   └── video                        # 比赛内录存储位置
│
├── include
│   ├── data_manager
│   │   ├── base.h                   # 基本全局参数
│   │   └── param.h                  # 调用json读取参数
│   ├── garage
│   │   ├── garage.h                 # 车库管理头文件
│   │   ├── interface.h              # 父类
│   │   ├── wrapper_car.h            # 标准装甲板车派生类
│   │   ├── wrapper_rune.h           # 打符派生类
│   │   └── wrapper_tower.h          # 前哨站基地派生类
│   │
│   └── threads
│       ├── control
│       │   ├── crc.h                # crc校验部分
│       │   └── structure.h          # 与下位机通讯结构体
│       │
│       ├── control.h                # 控制模块头文件
│       └── pipeline.h               # 流水线头文件
│
├── libs
│   └── json
│       
├── README.md
├── run.sh
│
└── src
    ├── data_manager
    │   ├── base.cpp
    │   ├── CMakeLists.txt
    │   ├── init.cpp
    │   └── param.cpp
    ├── garage
    │   ├── CMakeLists.txt
    │   ├── garage.cpp
    │   ├── wrapper_car.cpp
    │   ├── wrapper_rune.cpp
    │   └── wrapper_tower.cpp
    │
    ├── main.cpp    # 主函数，启动各线程并挂起自身
    │
    └── threads
        ├── CMakeLists.txt
        ├── control
        │   ├── crc.cpp
        │   ├── receive_thread.cpp               # 串口接收逻辑处理
        │   └── send_thread.cpp                  # 调用目标预测，弹道解算与控制发送
        ├── control.cpp                          # 基本收发函数
        ├── pipeline
        │   ├── baseline                         # 自瞄主流水线，使用标准yolo矩形
        │   │   ├── preprocessor_thread.cpp      # 流水线第一级，神经网络预处理与推入
        │   │   ├── detector_thread.cpp          # 流水线第二级，同步推理与NMS
        │   │   ├── tracker                      
        │   │   │   ├── pointer.cpp              # 查找四点
        │   │   │   ├── locater.cpp              # 位姿解算
        │   │   │   └── updater.cpp              # 模型更新
        │   │   └── tracker_thread.cpp           # 流水线第三级，运动模型更新
        │   │
        │   ├── fourpoints                       # 自瞄四点流水线，使用四点网络
        │   │   ├── preprocessor_thread.cpp      # 流水线第一级，预处理
        │   │   ├── detector_thread.cpp          # 流水线第二级，同步推理与NMS
        │   │   └── fourpoints.cpp               # 流水线第三级，运动模型更新
        │   │
        │   ├── rune                             # 打符四点流水线
        │   │   ├── preprocessor_thread.cpp      # 流水线第一级，预处理
        │   │   ├── detector_thread.cpp          # 流水线第二级，同步推理与NMS
        │   │   └── tracker_thread.cpp           # 流水线第三级，打符模型更新
        │   │
        │   ├── image_thread.cpp                 # 图像显示与debug
        │   ├── recording_thread.cpp             # 内录线程
        │   └── ui.cpp                           # ui线程
        │
        └── pipeline.cpp                         # 流水线启动函数
```





## 🎄 参数介绍 🎄



这里主要介绍 **Debug** 部分的参数功能，其余部分如有不理解可联系作者



```json
"Debug" : {
        "System" : {
            "AutoFire" : true,            // 自动开火
            "AutoEnemy" : true,           // 根据下位机提供颜色索敌，蓝0红1
            "AutoRune" : true,            // 根据下位机config控制打符
            "AutoCapture" : false         // 非自瞄状态录像
        },
        "ImageThread" : {
            "Scale" : 0.5,                // imshow缩放尺寸
            "FPS" : 25,                   // imshow帧率
            "UI" : true,                  // imshow开启UI
            "Imwrite" : false,            // 逐帧保存图像进行debug
            "Binary" : false,             // 二值化图像显示
            "Light" : false,              // imshow提亮
            "Histogram" : false           // 装甲板直方图显示
        },
        "Display" : {
            "Reprojection" : false,       // 装甲板贴纸重投影，需开启imshow
            "PipelineDelay" : false,      // 流水线每一级延迟
            "PointSkip" : false           // 打印灯条筛选条件
        },
        "Control" : {
            "Serial" : true,              // 开启下位机串口通讯线程
            "Timeout" : true,             // 线程超时自杀
            "ManuCapture" : false,        // 手动强制内录
            "ManuFire" : false,           // 手动开火
            "ManuRune" : false,           // 手动进入打符模式，调试专用
            "BigRune" : false             // 进入打符默认小符，开启后为大符
        },
        "PlusPnP" : {
            "Enable" : true,              // 上交提出的pnp方法，让我们感谢交爷
            "CostImage" : false           // 损失函数图，需开启imshow
        },
        "StateDelay" : {                  // 同步触发的替代品
            "Enable" : true,              
            "TimeS" : 0.05,
            "QueueSize" : 100,
            "SendWait" : 10
        },
        "SaveSpeed" : {                   // 英雄用弹速寄存器
            "SpeedWrite" : true,
            "SavePath" : "/etc/openrm/speed_log.txt"
        },

        "BigDecal" : "/etc/openrm/image/armor1.png",        // 重投影大装甲图像
        "SmallDecal" : "/etc/openrm/image/armor3.png"       // 重投影小装甲图像
    }
```

