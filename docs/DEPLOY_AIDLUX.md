# 在 AidLux 犀牛派 A1 / X1 上运行 TJURM-2024 + OpenRM 自瞄

> 目标板：Rhino Pi-A1（Qualcomm QCS6490，12 TOPS INT8）/ Rhino Pi-X1（Qualcomm QCS8550，48 TOPS INT8）
> 目标做法：**保留原有算法与流水线，把 TensorRT 推理换成 AidLite(QNN) 在 NPU 上跑**，其余（PnP、卡尔曼、串口、打符、UI）全部沿用。
> 本文档同时给出「为什么能改」「已经改成什么样」「AIMO 上怎么转模型」「板上怎么编怎么跑」「还需要你提供什么」。

---

## 0. 结论速览

| 问题 | 结论 |
| --- | --- |
| 能不能在无 CUDA 的犀牛派上跑这套自瞄？ | **能**。CUDA 只出现在 3 个地方：①TensorRT 推理 ②`cvtColor/resize` 的 GPU 预处理 ③相机锁页/显存缓冲。这三块都可以整体替换，算法主体（PnP 解算、卡尔曼/自瞄跟踪、串口、打符、参数面板）是**纯 CPU** 的，没有一处依赖 CUDA。 |
| NPU 能不能跑？ | **能**。AIMO 支持 ONNX→QNN（.ctx.bin），AidLite 的 `TYPE_QNN + TYPE_DSP/NPU` 就是走高通 HTP（Hexagon NPU）。仓库里的 `RP24.onnx` 只用到 Conv/Mul/Add/Clip/Div/Sigmoid/Reshape/Transpose/Resize/Gemm 这些标准算子，**没有自定义算子、没有动态 shape、没有把 NMS 放进图里**，属于 QNN 最容易转换的一类模型。 |
| 改了多少代码？ | 新增一个与后端无关的推理层 `rm::Infer`（OpenRM），TJURM 的三条流水线（baseline / fourpoints / rune）的「预处理器 + 检测器」共 6 个线程改成调用它；相机新增 UVC(MJPEG) 驱动；CMake 去掉 `CUDA REQUIRED`；`Config.json` 增加后端/模型/相机字段。详见第 3 节文件清单。 |
| 最大风险是什么？ | 不是"能不能跑"，而是**INT8 量化后的精度**（这个模型的检测头解码被一并量化），以及**AIMO 转换后输入张量的 layout 变成 NHWC**。前者用 W8A16/FP16 + 真实校准集解决，后者已在 `Config.json` 里做成可配（`InputLayout`）。 |
| 性能大概多少？ | A1：NPU 单帧 3~6 ms + CPU letterbox 4~8 ms ⇒ **70~120 FPS** 量级；X1：NPU 1.5~3 ms ⇒ 上限由 CPU 预处理决定。远高于比赛要求（120 发/分 ≈ 2 Hz 开火判决，跟踪侧 60+ FPS 足够）。 |

---

## 1. 现状：这套项目到底依赖什么

### 1.1 OpenRM（算法/驱动动态库）

| 依赖 | 用途 | 板上是否有 |
| --- | --- | --- |
| OpenCV ≥ 4.5.4 | 图像、PNP、绘图 | `sudo apt install libopencv-dev`（22.04=4.5.4，24.04=4.6.x） |
| Eigen3 | 卡尔曼 / 坐标变换 | `libeigen3-dev` |
| Ceres ≥ 2.x | `solver/polynomial.cpp` 多项式拟合 + `kalman/*` 里的 `ceres::Jet` 自动微分 | `libceres-dev`（**apt 版即可，不必按 README 源码编 2.2.0**） |
| ncurses | `openrm` 参数面板 | `libncurses-dev` |
| CUDA / TensorRT | `tensorrt/*`、`cuda/*`（NVCC 编译的 resize 核函数） | **没有** → 已改为可选，找不到就整个模块不编译 |
| 大恒 / 迈德威视 / 海康 SDK | 工业相机 | 没有 → 已改为可选，并补了空实现避免链接失败 |
| pthread | 线程 | 系统自带 |

### 1.2 TJURM-2024（自瞄框架）

| 依赖 | 原状态 | 现在 |
| --- | --- | --- |
| OpenRM | `find_package(OpenRM)` | 不变（多了 `openrm::openrm_infer`） |
| **CUDA** | `find_package(CUDA **REQUIRED**)` + `include_directories(/usr/local/cuda/include)` | **已删除** |
| OpenCV / Eigen / Ceres | 必需 | 不变 |
| nlohmann/json | 自带 `libs/json` | 不变 |
| `-lMVSDK` | 写死在 `threads`、`data_manager` 的 `target_link_libraries` 里 | **已删除**（否则板上必然链接失败） |

### 1.3 CUDA 到底卡在哪（改造前的 6 个硬点）

```
① TJURM/CMakeLists.txt                 find_package(CUDA REQUIRED)
② TJURM/include/threads/pipeline.h     cudaStream_t、nvinfer1::IExecutionContext* 成员
③ TJURM/src/threads/pipeline.cpp       rm::initCudaStream() ×2，失败即 exit
④ TJURM/src/threads/pipeline/*/preprocessor_thread.cpp
                                       rm::initTrtOnnx / initTrtEngine / mallocYoloDetectBuffer
                                       memcpyYoloCameraBuffer / resize(kernel) / detectEnqueue
⑤ TJURM/src/threads/pipeline/*/detector_thread.cpp
                                       detectOutput（cudaMemcpyAsync + StreamSynchronize）
⑥ TJURM/src/data_manager/init.cpp      rm::mallocYoloCameraBuffer（cudaMallocHost + cudaMalloc）
   OpenRM/include/openrm.h             无条件 #include <tensorrt/tensorrt.h>（没有 NvInfer.h 就编不过）
```

### 1.4 推理链路与坐标一致性（改造必须保持的部分）

原实现把空间关系写死在两处，改预处理时**必须同时满足**，否则框会整体偏移：

1. `cuda/src/resize/affine.cu`：`scale = min(dst_w/src_w, dst_h/src_h)`，**等比缩放 + 居中留灰边**，填充值 `114`，`BGR→RGB`，`/255.0`，`HWC→CHW`。
2. `src/tensorrt/nms.cpp::nms_set_ratio()`：

```cpp
ratio = max(input_w/infer_w, input_h/infer_h);
// 只有"窄"的方向才有 padding，另一边 padding = 0
x_input = x_infer * ratio - left_pad;
```

新的 CPU 预处理（`rm::letterboxToTensor`）严格复刻了同一套几何（等比、居中、114、RGB、/255、NCHW），所以 `nms.cpp` 一行都不用改就仍然对得上。

### 1.5 仓库里那个 ONNX 到底是什么（实测）

用自带的 ONNX 解析脚本（`tools/onnx_inspect.py`，不依赖 onnx 包）读出来：

```
== data/uniconfig/models/RP24.onnx ==
ir_version : 6          opset : ai.onnx 11       producer : pytorch
INPUT  : images  float32 [1, 3, 640, 640]        <-- NCHW
OUTPUT : output  float32 [1, 25200, 22]          <-- 22 = 8(四点) + 1(conf) + 4(颜色) + 9(ID)
节点数 : 444
算子分布: Conv 61, Mul 53, Add 36, Clip 26, Div 26, Reshape 24, Sigmoid 24,
          Gemm 18, Relu 15, GlobalAveragePool 9, Concat 9, Transpose 3, Resize 2, Constant 138
```

结论：
* 与 `Config.json` 里的 `FPX: InferWidth/Height=640, LocateNum=8, ColorNum=4, ClassNum=9, BboxesNum=25200, StructNum=22` **完全自洽**（解码已经烘进图里，NMS 在 CPU 做）。
* `GlobalAveragePool + Gemm` 是 SE 模块，`Sigmoid+Mul` 是 SiLU，`Clip+Div` 是 hardsigmoid —— 都是 QNN 支持的标准算子。
* ⚠️ **`RP24.onnx` 和 `rune.onnx` 的 SHA256 完全相同**（`0A535EB1…C7AB78`），说明打符模型只是被复制了一份占位，真正的 rune 模型需要你另外提供。

---

## 2. 目标平台与推理栈

| | Rhino Pi-A1 | Rhino Pi-X1 |
| --- | --- | --- |
| 芯片 | QCS6490（6nm，1×A78@2.7G + 3×A78@2.4G + 4×A55@1.9G） | QCS8550（1+2+3 Kryo，Adreno 740） |
| NPU | Hexagon HTP，**~12 TOPS INT8** | Hexagon HTP，**~48 TOPS INT8** |
| 内存 | 8 GB LPDDR4 | 16 GB LPDDR5X |
| 系统 | AidLux（Ubuntu 24.04）或 Ubuntu 24.04 纯净版 | AidLux（Android13+Ubuntu22.04）或 Ubuntu 22.04 |
| 推理栈 | **AidLite SDK**（`libaidlite.so`）+ QNN backend（`.ctx.bin`） | 同左 |

AidLite 关键点（官方文档）：
* 头文件 `/usr/local/include/aidlux/aidlite/aidlite.hpp`，库 `/usr/local/lib/libaidlite.so`
* `FrameworkType::TYPE_QNN`（或指定 `TYPE_QNN231/236/…`）、`AccelerateType::TYPE_DSP`（高通的 DSP 就是 HTP/NPU；`TYPE_NPU` 亦有）
* `ImplementType::TYPE_LOCAL`（推理在 Linux 侧本地完成，无 IPC 拷贝 —— 必须用这个）
* **SNPE/QNN 的输入输出在 SDK 层面统一是 float32**，量化在模型内部完成 —— 所以 NMS 拿到的仍是 float32，`nms.cpp` 无需改动

安装/自检：

```bash
sudo aid-pkg update
sudo aid-pkg install aidlite-sdk
sudo aid-pkg install aidlite-qnn231     # 版本要和 AIMO 导出时选的 Runtime 对齐
dpkg -l | grep aidlite                  # 确认 aidlite-sdk 与 aidlite-qnnXXX 的 2.3.x
python3 -c "import aidlite; print(aidlite.get_library_version())"
```

---

## 3. 已经做完的改动

### 3.1 新增：与后端无关的推理层 `rm::Infer`（OpenRM）

```
OpenRM/include/infer/infer.h         公开接口（后端无关）
OpenRM/include/infer/nms.h           YOLO 后处理（NMS）声明 —— 纯 CPU，原来挂在 tensorrt 里
OpenRM/src/infer/backend.h           后端抽象基类（内部）
OpenRM/src/infer/infer.cpp           letterbox 预处理 + 异步工作线程 + 后端工厂
OpenRM/src/infer/nms.cpp             NMS 实现（从 src/tensorrt/ 移出来，不再依赖 CUDA）
OpenRM/src/infer/nmsV5C36.cpp        36 类老格式 NMS（同上）
OpenRM/src/infer/backend_aidlite.cpp AidLite(QNN/SNPE) 后端  <-- 板上走这条
OpenRM/src/infer/backend_tensorrt.cpp TensorRT 后端         <-- Jetson 上仍可用
OpenRM/src/infer/backend_null.cpp    空后端（无可用后端时给出明确报错）
```

> 关键点：`nms.cpp` 原来放在 `src/tensorrt/` 下，而 `src/tensorrt` 只有检测到 CUDA 才会编译。
> 也就是说**原版在无 CUDA 的板子上，NMS 符号根本不存在**（TJURM 会链接失败）。
> 现在 NMS 归到 `openrm_infer`，永远参与编译。

对外只有 4 个动作，正好对应原来那 4 个 CUDA 调用：

| 原来（CUDA） | 现在 |
| --- | --- |
| `memcpyYoloCameraBuffer()` + `resize(kernel)` | `infer.setInput(cv::Mat)`（CPU/OpenCV，几何与 kernel 一致） |
| `detectEnqueue()` | `infer.enqueue()`（内部工作线程执行 `invoke()`） |
| `detectOutput()`（D2H memcpy） | `const float* out = infer.synchronize()`（已经就绪） |
| `initTrtEngine/initTrtOnnx/malloc*Buffer` | `infer.load(path, options)` |

后端选择：`Backend=AUTO` 时，`.engine` → TensorRT，其它（`.ctx.bin`/`.dlc`/`.onnx`/`.tflite`）→ AidLite。

预处理实现（`writeTensor`）：
`cv::resize`（等比）→ 640×640 画布居中贴图（填充 114）→ `BGR2RGB` → `convertTo(1/255)` → `split` 成 NCHW（或直接拷成 NHWC）。全部走 OpenCV 的 SIMD 路径，1280×1024→640×640 大约 4~8 ms（4 线程）。

### 3.2 OpenRM 其它改动

| 文件 | 改动 |
| --- | --- |
| `CMakeLists.txt` | `find_package(CUDA)` 改为可选；新增 `OPENRM_WITH_AIDLITE`（自动 `find_path`/`find_library` 探测）；新增 `src/infer` 子目录与 `openrm_infer` 目标 |
| `cmake/OpenRMConfig.cmake.in(.nocuda)` | 导出 `openrm::openrm_infer` |
| `include/openrm.h` | `#if __has_include(<NvInfer.h>)` 才 include tensorrt 头；新增 `<infer/infer.h>` |
| `src/video/uvc.cpp` | 新增 `openUVCExt(..., pixel_format)` 支持 **MJPEG/YUYV**；修了「先 QBUF 再解码」的悬空读；帧率真正按参数设置；宽高以驱动返回值为准 |
| `src/video/stub_camera.cpp` | **新增**：没装大恒/迈德威视/海康 SDK 时提供同名空实现，避免 TJURM 链接失败 |
| `src/video/CMakeLists.txt` | 编译 stub + `HAVE_*` 宏；原条件编译逻辑保留 |
| `include/structure/camera.hpp` | `capture_format`、`uvc_capture` 字段 |
| `src/tensorrt/nms.cpp` | FPX 的颜色循环不再硬编码 3（`ColorNum=4` 时第 4 个颜色通道原本被忽略），`yolo_size` 由 `colors_num` 计算 |
| `src/uniterm/{dashboard,monitor,oscilloscope}.cpp` | `#define NCURSES_NOMACROS`，**不再需要按 README 去改 `/usr/include/curses.h` 把 `OK` 改成 `KO`** |
| `run.sh` | 后端探测提示、`ln -sf`、`ldconfig` |

### 3.3 TJURM 改动

| 文件 | 改动 |
| --- | --- |
| `CMakeLists.txt` | 删除 `find_package(CUDA REQUIRED)` 与 cuda include |
| `src/{threads,data_manager}/CMakeLists.txt` | 删除写死的 `-lMVSDK` |
| `include/threads/pipeline.h` | `cudaStream_t/nvinfer1::IExecutionContext*` → `std::shared_ptr<rm::Infer> armor_infer_/rune_infer_` |
| `src/threads/pipeline.cpp` | 删除 4 处 `initCudaStream` |
| `src/threads/pipeline/{baseline,fourpoints}/preprocessor_thread.cpp`、`rune/preprocessor_thread.cpp` | `rm::Infer::load/setInput/enqueue`；**先等上一帧被消费完**再取新帧（避免覆盖推理输入/输出缓冲）；用 `std::atomic_store` 把引擎发布给检测线程 |
| `src/threads/pipeline/{baseline,fourpoints}/detector_thread.cpp`、`rune/detector_thread.cpp` | `rm::Infer::synchronize` + 原 NMS；FPX 传 `color_num`；**NMS 跑完才释放握手标志**（RAII `FlagGuard`），保证生产者不会在 NMS 读输出时覆盖它 |
| `include/data_manager/model.h` | **新增**：把 `Config.json` 的 `Model` 节点翻成 `rm::InferOptions`（含旧配置 `DirEngine/DirONNX` 兼容） |
| `src/data_manager/init.cpp` | 相机驱动可配（`UVC`/`MindVision`/`DaHeng`/`HikVision`），UVC 自动枚举 + `openUVCExt` + `runUVC`；去掉 CUDA 缓冲；内参缺失直接报错退出；串口回退到 `ttyUSB*` |
| `src/main.cpp` | `cv::setNumThreads(Config.Debug.System.CvThreads)`（默认 4，避免和 NPU 驱动抢核） |
| `data/uniconfig/Config.json` | 新增 `Camera.Driver/UVCName/Height/Device/PixelFormat/BufferNum/ApplyV4L2Controls`、`Model.*.DirModel/Backend/InputLayout/InputType/StructNum/Framework/Accelerate/QNNVersion/HtpConfig/Threads`；`Serial/Timeout` 默认 false（首次自检用） |
| `data/uniconfig/CamLens.json` | 新增 `UVC1280_1024`、`UVC720_540` 两组内参（先复制自现有标定，**必须重新标定后覆盖**） |
| `data/uniconfig/guard.sh` | `nvpmodel/jetson_clocks` 不存在时跳过（板上没有这两个命令） |
| `run.sh` | `-b` 只编译不运行；创建 `/etc/openrm/{video,debug}`；错误码检查 |

---

## 4. AIMO 上转换模型的完整流程

AIMO = https://aidlux.com/product/aimo （SaaS，网页版；也可私有化 Docker）。它把 ONNX 转成高通 QNN 的 `.ctx.bin`（同时给出 `htp_backend_ext_config.json` 等）。

### 4.1 转换前：先自查 ONNX（很重要）

```bash
python3 tools/onnx_inspect.py path/to/your.onnx
```

必须满足：

| 检查项 | 要求 | 你现在这份 |
| --- | --- | --- |
| 输入固定 shape、batch=1 | `[1,3,640,640]` | ✅ |
| 输出是「解码后」的张量 | `[1,N, 8+1+4+9]` | ✅ `[1,25200,22]` |
| 图里**不要**有 NMS / TopK / 动态 shape | — | ✅ 没有 |
| opset ≤ 13 | QNN 转换器对 opset 敏感 | ✅ 11 |
| 前后处理在图外 | letterbox(RGB,/255,114) 与 NMS 都在 C++ | ✅ |
| 类别/颜色顺序与 `ClassMap/ColorMap` 一致 | 见下 | ⚠️ 需你确认 |

`ClassMap/ColorMap` 的语义：`class_id` 是网络输出的下标，`ClassMap[class_id]` 才是装甲板 ID；`ColorMap[color_id]` 才是红/蓝/紫。当前配置 `ColorMap=[1,0,2,3]`（说明网络第 0 个颜色通道是"红"）—— **换模型后必须重新核对**，否则会红蓝反。

### 4.2 AIMO 操作步骤

1. **上传模型**：源格式选 `ONNX`，上传 `RP24.onnx`。
2. **选择目标平台**：
   * A1 → 芯片 `Qualcomm QCS6490`；X1 → `Qualcomm QCS8550`
   * Runtime：选与板上 `dpkg -l | grep aidlite` 一致的 QNN 版本（A1 出厂常见 **QNN 2.31 / 2.36**）
   > 选错 Runtime 的后果：AidLite 加载时报 `unsupported context binary` 或直接崩。
3. **参数设置**：见下面 4.2.1 的逐项对照表。

#### 4.2.1 转换页面逐项对照（实测）

| 页面项 | 应该填什么 | 原因 |
| --- | --- | --- |
| 输入 名称/维度 | `images` / `1, 3, 640, 640` | ✅ 与 ONNX 一致，**说明 AIMO 保留了 NCHW**，`Config.json` 里 `InputLayout` 先填 `NCHW` |
| 输出 名称/维度 | `output` / **`1, 25200, 22`**（页面里维度是空的，**必须补上**） | 输出维度缺失时量化会按错误范围算 scale，且后端可能拿不到完整输出 |
| **量化（右上角开关）** | **打开** | 现在是关的（整块是灰的）。关着转出来是不量化模型，HTP 上跑不起来或极慢 |
| 数据精度 | 先试 `INT16`（=W8A16）；没有就 `INT8`；掉点严重用 `FP16` | 解码头（Sigmoid/Mul/Resize）对 INT8 敏感，A1 的 HTP 支持 16bit 激活 |
| 量化模式 | 保持 `CLE` 勾选，`ADA` 不勾，`启用通道量化` 可留默认 | CLE = Cross-Layer Equalization，官方推荐默认值 |
| 校准辅助数据 | `Image` | — |
| **数据集** | **改选 `Custom`，上传自己拍图** | ⚠️ 现在是 `ImageNet`。ImageNet 图和赛场装甲板毫无关系，用它校准等于没校准 |
| 色彩模式 | `RGB` | ✅ C++ 端 letterbox 之后就是 RGB |
| **均值** | **`0, 0, 0`** | ⚠️ 现在填的是 ImageNet 的 `123.675/116.28/103.53`。本模型预处理只有 **除以 255**，没有减均值 |
| **方差（标准差）** | **`255, 255, 255`** | ⚠️ 现在填的是 ImageNet 的 `58.395/57.12/57.375`（=0.229×255）。要让校准输入 = 推理输入 = `pixel/255`，标准差就是 255 |

> 为什么均值/方差这么填：AIMO 用 `(pixel - 均值) / 方差` 把校准图变成模型输入张量。
> 我们的模型输入是 `pixel / 255`，等价于 `(pixel - 0) / 255`。
> 如果沿用 ImageNet 那组数，校准张量会落在 `[-2.1, 2.3]`，而真实推理张量是 `[0, 1]`，
> 量化器按错的动态范围算 scale，精度会明显下降（而且**不报错**，很难发现）。
>
> 例外：如果你的训练脚本用的是 ImageNet 归一化（`(x/255 - mean)/std`），那要改的是
> C++ 的预处理（`rm::writeTensor`），不是这里 —— 请先确认训练侧的预处理。

4. **转换** → 结果页可以对比转换前后结构，并跑 **精度评估（Accuracy Evaluation）**。
   结果页里再确认一次输入维度：若显示成 `1, 640, 640, 3`，则 `Config.json` 的
   `InputLayout` 改成 `NHWC`（页面左侧的 `1,3,640,640` 是 ONNX 侧的解析结果，
   最终 QNN 模型以结果页为准）。

5. **下载**：得到类似
   ```
   RP24_qcs6490_w8a16_qnn231/
     ├── RP24_qcs6490_w8a16_qnn231.ctx.bin     <-- 主文件
     ├── htp_backend_ext_config.json           <-- 有时会有，填到 Config 的 HtpConfig
     └── README.md
   ```
   放到板上：`sudo cp *.ctx.bin /etc/openrm/models/`，然后改 `Config.json`：

   ```json
   "FPX" : {
       "DirModel"   : "/etc/openrm/models/RP24_qcs6490_w8a16_qnn231.ctx.bin",
       "Backend"    : "AIDLITE",          // 明确指定，避免 AUTO 猜错
       "InputLayout": "NCHW",             // 以 AIMO 结果页显示的输入 shape 为准！见 4.4
       "InputType"  : "FLOAT32",          // QNN 的 SDK 层输入输出固定 float32
       "Framework"  : "QNN",              // 或 "QNN231" 精确锁定版本
       "Accelerate" : "DSP",              // 高通的 NPU 就是 DSP(HTP)
       "QNNVersion" : "2.31",
       "HtpConfig"  : "",                 // 若下载包里有 json 就填绝对路径
       "StructNum"  : 22,
       "BboxesNum"  : 25200
   }
   ```

### 4.3 校准数据怎么准备（最容易被忽略的一步）

* **来源**：必须用**赛场同一台相机、同一个镜头、同一曝光**拍的真实图，而不是网图/COCO。
* **数量**：`200~400` 张。少于 100 张，INT8 的置信度会明显塌。
* **覆盖**：不同距离（2m/5m/8m+）、不同角度、红蓝双方、灯光/逆光/暗场、部分遮挡、运动模糊、以及**画面里没有装甲板的负样本**（约 1/5）。
* **尺寸与预处理**：AIMO 会按模型输入尺寸处理校准图。为了避免"插值方式/letterbox 归一化"与推理时不一致，建议**先离线把图 letterbox 到 640×640（等比 + 114 灰边）**再上传。仓库里可以直接用 OpenCV 一行做：

  ```python
  import cv2, numpy as np, glob, os
  os.makedirs('calib', exist_ok=True)
  for i, p in enumerate(glob.glob('raw/*.jpg')):
      img = cv2.imread(p)
      h, w = img.shape[:2]
      s = min(640/w, 640/h)
      nw, nh = round(w*s), round(h*s)
      canvas = np.full((640,640,3), 114, np.uint8)
      canvas[(640-nh)//2:(640-nh)//2+nh, (640-nw)//2:(640-nw)//2+nw] = cv2.resize(img,(nw,nh))
      cv2.imwrite(f'calib/{i:04d}.jpg', canvas)
  ```
* **顺序无关**，但不要全是同一帧的连拍（多样性比数量更重要）。

### 4.4 转换后必须核对的两件事

1. **输入张量 layout**：AIMO/QNN 为了 HTP 效率，常常把 NCHW 转成 **NHWC**。结果页会显示转换后的 `input: [1,640,640,3]`。
   * 若显示 `[1,3,640,640]` → `"InputLayout": "NCHW"`
   * 若显示 `[1,640,640,3]` → `"InputLayout": "NHWC"`
   * 搞错的症状：程序不报错，但检测框全是乱的/几乎检不到 —— 这是最常见的一个坑。
2. **输出张量 shape**：应为 `[1,25200,22]`。若被拆成 3 个输出（`[1,80,80,22]` 之类），说明导出时没做 concat/transpose，`rm::Infer` 目前只取 `output[0]`，需要改模型导出（推荐）或改后端（不推荐）。

### 4.5 精度验收

* AIMO 结果页的 **精度评估**：和原始 ONNX 的逐层/端到端误差。
* 板上实测（推荐）：`Config.Debug.ImageThread.Imwrite=true` 逐帧存图，跑一段录像回放，对比 ONNX(PC, onnxruntime) 与 QNN 的 `conf` 分布与框位偏差。经验阈值：四点框中心偏差 < 1.5 px、conf 平均下降 < 0.05 视为可用。
* 掉点的常见原因与对策：
  | 现象 | 原因 | 对策 |
  | --- | --- | --- |
  | 置信度整体偏低、漏检 | 校准集不代表现场 | 换真实图，加负样本 |
  | 红蓝识别错乱 | `ColorMap` 没跟着改 | 核对 `ColorMap`，必要时用 `Debug.ImageThread.Histogram` 看 |
  | 框抖动/偏移 | 解码头被量化 | 改 W8A16 / FP16 |
  | 小装甲板检不到 | 输入分辨率降了 | 保持 640×640，别为省算力降到 480 |

---

## 5. 板上编译与部署

### 5.1 依赖

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config \
    libopencv-dev libeigen3-dev libceres-dev libncurses-dev \
    v4l-utils      # 调 UVC 曝光/格式用 v4l2-ctl
# AidLite
sudo aid-pkg update && sudo aid-pkg install aidlite-sdk && sudo aid-pkg install aidlite-qnn231
```

> 不需要 CUDA / TensorRT / 大恒 / 迈德威视 SDK。
> README 里「改 `/usr/include/curses.h` 把 OK 改成 KO」那一步**已经不需要了**（代码里 `#define NCURSES_NOMACROS`）。

### 5.2 编译安装 OpenRM

```bash
cd OpenRM-main
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
# 关键输出：应能看到
#   AidLite found: /usr/local/lib/libaidlite.so
cmake --build build -j$(nproc)
sudo cmake --install build
sudo ldconfig
```

若 `AidLite NOT found`：确认头文件/库路径，或显式指定
`-DOPENRM_AIDLITE_INCLUDE_DIR=/usr/local/include -DOPENRM_AIDLITE_LIBRARY=/usr/local/lib/libaidlite.so`。

### 5.3 编译 TJURM

```bash
cd TJURM-main
sudo ./run.sh -b          # 只编译，不启动
```

### 5.4 首次运行（分阶段验证，别一上来接整车）

编辑 `/etc/openrm/Config.json`（`config/` 就是它的软链接）：

```jsonc
"Debug": { "Control": { "Serial": false, "Timeout": false } }   // 先断开下位机
"Debug": { "ImageThread": { "UI": true } }
"Model": { "YoloArmor": { "Type": "FPX", "FPX": { "DirModel": ".../RP24_xxx.ctx.bin" } } }
```

```bash
v4l2-ctl -d /dev/video0 --list-formats-ext      # 看支持的 MJPEG/分辨率/帧率
TJURM-2024 -s                                   # 带 imshow 启动（需有 X/虚拟屏）
```

启动日志按顺序应出现：

```
Video UVC opened: /dev/video0
Video UVC size: 1280x1024
Lens calibration loaded: UVC1280_1024/Prime6mm
Infer ready: AidLite <- /etc/openrm/models/RP24_xxx.ctx.bin
```

看到 `Infer ready` 后，`Debug.Display.PipelineDelay=true` 会打印 `preprocess` / `detect time`(ms)，据此判断瓶颈在 CPU 还是 NPU。

---

## 6. 性能预估与调优

| 环节 | A1 (QCS6490) | X1 (QCS8550) | 备注 |
| --- | --- | --- | --- |
| MJPEG 解码 1280×1024 | 4~6 ms | 3~4 ms | 换 YUYV 会吃 USB 带宽（1280×1024@60 ≈ 157 MB/s） |
| letterbox → 640×640 NCHW float | 4~8 ms | 3~6 ms | `CvThreads=4`，可试 2/6 |
| NPU 推理（W8A16, 640×640） | 3~6 ms | 1.5~3 ms | AIMO 结果页会给单帧耗时 |
| NMS + 筛选（CPU） | 1~2 ms | 1~2 ms | 25200 框，单线程排序 |
| 合计（端到端） | **~12~20 ms** | **~8~12 ms** | 对应 50~80 FPS / 80~120 FPS |

调优顺序：
1. `Config.Camera.Base.Width/Height` 降到 1280×720（**同时要重标内参**），预处理立刻省 3~4 ms。
2. `CvThreads` 试 2 / 4 / 6，别超过小核数。
3. 想进一步压延迟：把「预处理」和「推理」做成双缓冲（两个 `rm::Infer` 实例轮转，QNN 支持多 graph 实例），可再省 3~5 ms。当前实现是单输入缓冲、`setInput` 会等上一帧推理结束（为保证正确性做的保守选择）。
4. 不建议为了速度把模型输入降到 480×480 —— 小装甲板召回会掉。

---

## 7. 风险清单

| 风险 | 影响 | 处理 |
| --- | --- | --- |
| QNN 版本不匹配 | 模型加载失败 | AIMO 的 Runtime 与 `aidlite-qnnXXX` 对齐 |
| 输入 layout 判断错 | 检测全乱但不报错 | 4.4 节核对；`InputLayout` 一行改完 |
| INT8 解码头掉点 | 漏检/抖动 | W8A16 / FP16；真实校准集 |
| `rune.onnx` 是占位文件 | 打符必然用不了 | 需要你提供真模型（见第 8 节） |
| 内参没重标 | 角度系统性偏差 | 用棋盘格重标，覆盖 `CamLens.json` 的 `UVC1280_1024` |
| UVC 曝光不可控 | 运动模糊/过曝 | `ApplyV4L2Controls=true` + `v4l2-ctl` 手动设；不行就换工业相机（大恒/迈德威视的 ARM64 SDK 也能装，`openrm_video` 的条件编译会自动启用） |
| AidLite API 细节与文档不完全一致 | 编译报错 | 中风险点集中在一个文件：`OpenRM/src/infer/backend_aidlite.cpp`。该文件已刻意只用"整型强转枚举值 + 文档里出现过的类名/方法名"，若仍报错只需按你机器上的 `aidlite.hpp` 改这一个文件（下面 7.1 列了要对齐的 6 处） |
| 单输入/单输出缓冲 | 预处理与推理不能重叠 | 见第 6 节第 3 条，可后续做双缓冲 |
| `autoaim_combine()`（自瞄+打符同开） | 该函数把 4 个线程塞进同一个握手槽，会串帧 | 目前 `main.cpp` 里是注释掉的（只用 `autoaim_baseline`），**先不要打开**；要用需要给打符单独一套 `mutex/flag/register` |

### 7.1 板上编译时 `backend_aidlite.cpp` 需要对照头文件的 6 处

```bash
grep -n "class Interpreter\|set_input_tensor\|get_output_tensor\|set_model_properties" \
     /usr/local/include/aidlux/aidlite/aidlite.hpp
```

| 行号（当前代码） | 我假设的签名 | 若不一致怎么改 |
| --- | --- | --- |
| 132 | `Model* Model::create_instance(const char*)` | 改成实际同名工厂 |
| 158 | `int set_model_properties(vec<vec<uint32_t>> in_shape, DataType in_dt, vec<vec<uint32_t>> out_shape, DataType out_dt)` | 若 SDK 要求 `std::vector<DataType>`，把两个 `DataType` 实参包成 `{...}` |
| 166 | `Config* Config::create_instance()`，字段 `framework_type / accelerate_type / implement_type / is_quantify_model / number_of_threads / backend_extension_config` | 字段名以头文件为准 |
| 181 | `InterpreterBuilder::build_interpreter_from_model_and_config(Model*, Config*)` 返回 `unique_ptr<Interpreter>` | — |
| 209 / 222 | `int set_input_tensor(uint32_t, void*)` / `void* get_output_tensor(uint32_t)` | 若多一个 size 参数，补 `input_bytes_` / `output_.size()*sizeof(float)` |
| 174 | `ImplementType::TYPE_LOCAL == 3` | 已用整型强转，只有类名可能不同（`ImplementType` vs `ImplementedType`） |


---

## 8. 需要你提供 / 确认的信息

### 必须（否则只能盲改）

1. **板子和系统**：A1 还是 X1？是 AidLux 融合系统还是纯净 Ubuntu？把下面两条的输出贴我：
   ```bash
   uname -a; cat /etc/os-release | head -3
   dpkg -l | grep -i aidlite
   ls -l /usr/local/include/aidlux/aidlite/aidlite.hpp /usr/local/lib/libaidlite.so
   ```
2. **真正的模型文件**：仓库里的 `RP24.onnx` / `rune.onnx` 是不是最终模型（两者字节完全相同，明显占位）。请给出：
   * 训练用的类别顺序（9 类分别是谁）+ **颜色通道顺序**（0 是红还是蓝），用来核对 `ClassMap/ColorMap`；
   * 输入尺寸是否就是 640×640；
   * 输出是否就是 `[1,25200,22]`（用 `tools/onnx_inspect.py` 一跑就知道）。
3. **校准集**：200~400 张赛场实拍原图（1280×1024，最好是同一台相机同一镜头）。没有它，INT8 的精度只能靠运气。

### 有了更好

4. **相机**：型号、分辨率、帧率，是 USB(UVC) 还是 MIPI/工业相机？如果是 USB，把 `v4l2-ctl -d /dev/video0 --list-formats-ext` 的输出给我。
5. **AIMO 转换结果**：结果页的「模型结构/IO 信息」截图，或下载包里的 `README.md` / `htp_backend_ext_config.json`。我要确认 `InputLayout` 和是否有额外 backend 配置。
6. **串口**：下位机接的是板载 RS232 还是 USB-TTL？`ls /dev/ttyACM* /dev/ttyUSB* /dev/ttyS*`；波特率是否仍为 115200。
7. **重标内参**：能否在板上用棋盘格重标一次（我可以给标定脚本），覆盖 `CamLens.json` 里的 `UVC1280_1024`。

---

## 9. 附：改动后的目录速查

```
OpenRM/include/infer/infer.h               ← 新的推理接口（后端无关）
OpenRM/src/infer/*                         ← AidLite / TensorRT / Null 三个后端 + NMS
OpenRM/src/video/uvc.cpp                   ← UVC + MJPEG
OpenRM/src/video/stub_camera.cpp           ← 工业相机 SDK 缺失时的空实现
TJURM/include/data_manager/model.h         ← Config.json → InferOptions
TJURM/src/data_manager/init.cpp            ← 相机驱动分发（UVC / 工业相机 / 视频回放）
TJURM/src/threads/pipeline/{baseline,rune,fourpoints}/*.cpp
TJURM/data/uniconfig/Config.json           ← 新增 Backend/DirModel/InputLayout/Driver…
tools/onnx_inspect.py                      ← ONNX IO 与算子分布自查脚本
tools/prepare_calib.py                     ← 校准集 letterbox 脚本
tools/board/*                              ← 板上部署/自检脚本（见第 10 节）
```

---

## 10. 实测记录（2026-10-28，Rhino Pi-A1）

### 10.1 环境（实测）

| 项 | 实测值 |
| --- | --- |
| 系统 | Ubuntu 24.04.3 LTS（AidLux），aarch64，8 核，7.2 GB |
| 工具链 | g++ 13.3.0 / cmake 3.28.3 |
| OpenCV | 4.6.0（apt） |
| AidLite | `aidlite-sdk 2.3.2.248` + `aidlite-qnn236`（**QNN 2.36**） |
| Eigen / Ceres | 3.4.0 / 2.2.0（apt） |
| 相机 | **未接 UVC**；`/dev/video0,1` 是高通 camera 子系统的 `cam-req-mgr` / `cam_sync` 节点，不是摄像头 |

编译结果：**OpenRM 与 TJURM 均一次编译通过**（修掉 2 处我在移植时引入的小错后）。
落地路径：OpenRM → `/usr/local/{lib,include/openrm}`，TJURM → `/usr/local/bin/TJURM-2024`，
参数 → `/etc/openrm/`。

### 10.2 链接时的一个坑（已写进 CMake）

apt 版 OpenCV 的 `videoio` 依赖 `libavutil.so.58`，而它引用了**带版本号的 OpenCL 符号**
（`clXXX@OPENCL_1.0`），系统的 `libOpenCL.so.1` 没有导出符号版本，链接器直接报
`undefined reference`。系统里其实有 `libOpenCL.so` 和 `libOpenCL_adreno.so.1`，但版本不匹配。
**处理**：`TJURM/CMakeLists.txt` 加 `-Wl,--allow-shlib-undefined`（我们不用 avutil 的 OpenCL 路径）。

### 10.3 模型 IO（实测，与配置无关，运行时自动探测）

```
AidLite 版本 : Aidlux_Aidlite_C4L_V2.3.2.248
license      : the device is licensed, license ID aidd0842b...
IN   name=images type=UINT8  elems=1228800 shape=[1,640,640,3]   ← NHWC + UINT8
OUT  name=output type=UINT8  elems=554400  shape=[1,25200,22]    ← 拿到的是 float32(554400)
```
* `.aidem`（AIMO 下载包里的加密模型）**可以直接加载**，不需要额外解密。
* QNN 2.36 的运行时**能加载 2.31 转出来的 ctx**（实测 init/load 均 rc=0），不必重转。
* 输入是 **NHWC + UINT8**（不是 ONNX 页面显示的 NCHW）。`rm::Infer` 会在 `load()` 时
  用 `get_input_tensor_info()` 自动覆盖 `Config.json` 里的 `InputLayout/InputType`，
  所以即使配置写错也能自愈（日志里会打印 `AidLite model IO: input NHWC 640x640`）。

### 10.4 性能（实测，单帧）

| 环节 | A1 实测 |
| --- | --- |
| CPU letterbox 1440×1080 → 640×640 NHWC u8 | **5.55 ms** |
| `set_input_tensor` (uint8 native) | 0.30 ms |
| **NPU `invoke()`** | **3.26 ms** |
| `get_output_tensor`（反量化 554400 个 float） | 1.46 ms |
| 合计（不含 NMS） | **≈ 10.6 ms** → 约 90 FPS 上限 |

> 若改用 float32 输入（`is_native=false`），`set_input_tensor` 会涨到 3.82 ms，合计 12.4 ms。
> 所以配置应保持 `InputType: "UINT8"`。

### 10.5 ⚠️ 本次导出的 `w8a8` 模型不可用（实测证据）

用录像里一帧**明确有蓝色哨兵装甲板**的画面（`demo.avi` 第 120 帧）实测：

```
逐通道统计（25200 条 × 22 通道）:
  ch0~ch7 (四点坐标) : 0 ~ 812        ← 只有 34 个台阶（步长 24.614）
  ch8    (conf)      : 恒为 0.000
  ch9~ch21(颜色/ID)  : 恒为 0.000
原始 uint8 输出     : range [186, 219]，mean 191.7，record0 = 187 187 187 ...
```

反量化关系是 `value = (raw - 186) × 24.614`，即输出张量的量化步长是 **24.614**。
而置信度是 0~1 —— 在这个刻度上**任何置信度都会量化成 0**。也就是说：

> **这份 W8A8 模型在结构上就无法输出非零置信度**，与输入编码、代码都无关。

原因与对策：

| 原因 | 说明 | 对策 |
| --- | --- | --- |
| 输出是一个"混合张量"（坐标 0~640 + 置信度 0~1 挤在同一个 22 通道里），而 AIMO 默认**per-tensor INT8** | 一个 scale 要同时覆盖 640 和 0.001，置信度必然被抹平 | 改 **W8A16（INT16 激活）**：640/32767 ≈ 0.02，置信度可表示；或勾选 **启用通道量化** 让 22 个通道各自一个 scale |
| 校准集用了 **ImageNet** + **ImageNet 均值/方差**（截图里仍是默认值） | 校准输入分布和真实推理分布完全不同，激活范围被拉爆（实测输出量程到 ±3125） | 数据集改 **Custom** 上传真实图；均值 `0,0,0`、方差 `255,255,255` |

**结论：请在 AIMO 上按 4.2.1 的表格重新导出一份（优先 W8A16 / INT16），换掉
`/etc/openrm/models/` 里的文件即可，代码不用动。**

### 10.6 板上调试用的"视频文件当相机"

`Config.json` 里把驱动换成 `"Driver": "Video"` 就能拿录像回放跑整条流水线（台架调试/复现问题用）：

```json
"Camera": {
  "Driver": "Video",
  "Base": { "VideoPath": "/root/smoke/demo.avi", "Loop": true, "FrameRate": 30.0 }
}
```

实测启动日志（无相机也能跑通）：

```
Camera driver: video replay
Video replay: /root/smoke/demo.avi 1440x1080 loop=1
Lens calibration loaded: UVC1280_1024/Prime6mm
AidLite model IO: input NHWC 640x640, output 25200x22
Infer ready: AidLite <- /etc/openrm/models/0526_qcs6490_w8a8.qnn231.ctx.bin.aidem
```

### 10.7 板上脚本

| 脚本 | 作用 |
| --- | --- |
| `tools/board/build_openrm.sh` | 配置+编译 OpenRM（含 AidLite / 海康 SDK 探测） |
| `tools/board/build_tjurm.sh` | 安装 OpenRM + 编译 TJURM |
| `tools/board/deploy.sh` | 铺 `/etc/openrm`、拷模型、装二进制 |
| `tools/board/final_run.sh` | 一键：同步配置+装二进制+冒烟运行 |
| `tools/board/build_run.sh` | 编译并运行任意 AidLite 自检程序（冒烟/逐通道/计时） |
| `tools/board/build_hik.sh` + `hik_probe.cpp` | 海康相机直连自检：枚举/打开/取帧存 PPM |
| `tools/board/hik_rm_bench.cpp` | 走 OpenRM 驱动量相机真实帧率 |
| `tools/board/probe_usb.sh` / `usb_probe.cpp` | USB 层诊断（libusb 直接读 U3V） |
| `tools/board/aidlite_smoke.cpp` | 最小推理冒烟：模型能否加载 + 单帧耗时 |
| `tools/board/test6.cpp` | 逐通道统计，判断"模型是否活着/量化是否合理" |
| `tools/board/dumpframes.cpp` | 从录像里抽帧存 jpg |

---

## 11. 海康 MV-CS016-10UC 接入实录

### 11.1 这颗相机不是 UVC

* `lsusb` 能看到 `2bdf:0001 Hikrobot MV-CS016-10UC`，**但不生成 `/dev/video*`**；
* 它是 **USB3 Vision** 设备（`bcdUSB 3.20`，3 个 `EF/05` 接口），内核 `uvcvideo` 不接管，必须走厂商 SDK；
* 板上原有 `/dev/video0,1` 是高通 camera 子系统的 `cam-req-mgr` / `cam_sync`，**不是摄像头**，别被误导。

### 11.2 只拷 `libMvCameraControl.so` 会"枚举不到设备"

实测：把单个 `libMvCameraControl.so` 放进去后，`MV_CC_EnumDevices(MV_USB_DEVICE)`
返回 `0x80000006`（MV_E_NODATA，无设备），而 `lsusb` 明明能看到相机。

原因：该 .so 内部会 **`dlopen("libMvUsb3vTL.so")`** —— USB3 Vision 传输层是独立库。
只拷主库时，U3V 传输层缺失 ⇒ 静默枚举出 0 个设备。

**正确做法**：整套 SDK 的 `lib/arm64/` 一起装（版本要自洽）：

```bash
adb push <SDK>/lib/arm64/. /usr/local/lib/     # 5 个 .so
ldconfig
```
| 文件 | 作用 |
| --- | --- |
| `libMvCameraControl.so` | 主库 |
| `libMvUsb3vTL.so` | **USB3 Vision 传输层（缺了就枚举不到）** |
| `libFormatConversion.so` / `libMediaProcess.so` / `libMVRender.so` | 格式转换/处理/渲染 |

装齐后：`EnumDevices rc=0x00000000 n=1`，`MV-CS016-10UC serial=DA9239566`，
`1440x1080`，`PixelFormat=0x01080009`（BayerRG8），曝光/增益/帧率都能写。

### 11.3 SDK 版本差异（代码里已兼容）

| 现象 | 说明 | 处理 |
| --- | --- | --- |
| `undefined reference to MV_CC_Initialize / MV_CC_Finalize` | 3.x 的 .so 没有这两个符号（4.x 才有） | `hikvision.cpp` 里用 `dlsym()` 可选调用 |
| `undefined reference to MV_CC_ConvertPixelTypeEx` | 3.x 只有 `MV_CC_ConvertPixelType` + `MV_CC_PIXEL_CONVERT_PARAM` | 回调里改用 3.x 的 API |
| 头文件与 .so 版本不一致 | 头是 4.x/5.x、库是 3.2 | 用**同一套 SDK** 的 `include/` 覆盖 `include/video/hikvision/` |

另外修掉了 OpenRM 原本 `hikvision.cpp` 的两个 bug：
1. `dev_list.pDeviceInfo[device_num]` 用了 **1-based 下标**（越界）→ 改 `device_num - 1`；
2. `openHikVision()` **没有创建 `camera->buffer`**，回调里 `buffer->push()` 会崩 → 补上，
   并在回调注册前设置好 `camera->camera_id`。

### 11.4 实测性能（A1 + MV-CS016-10UC @1440x1080）

| 环节 | 实测 |
| --- | --- |
| 相机采集（含 BayerRG8→BGR 转换） | **59.2 fps**（16.65 ms/帧） |
| 预处理（letterbox 1440×1080 → 640×640 NHWC u8） | 9~17 ms |
| 推理 + NMS | 4~10 ms |
| **整条流水线** | **45 fps（22.2 ms/帧）** |

> 注意：`Debug.ImageThread.Imwrite=true` 时存图线程只有 ~4 fps，那是**存图线程自己的吞吐**，
> 不代表流水线速度，别拿它判断性能。

### 11.5 当前卡点：相机画面全黑

`hik_probe` 取原始 Bayer 流：曝光 5ms/增益16 → max=15；曝光 100ms/增益16 → max=69（255 满量程）。
流水线落盘的调试图也是纯黑（只有代码画的上十字准星）。

也就是说 **相机在正常出流，但几乎没有进光**。需要现场确认：

1. 镜头盖是否摘掉、镜头是否装好（CS016 是 C 口/CS 口，需自带镜头）；
2. 光圈是否打开；
3. 镜头是否对着明亮的场景（而不是桌角/暗处）；
4. 若仍黑，试 `ExposureTime` 加到 20000~50000 us、`Gain` 加到 16，并确认没有开 `TriggerMode`。

相机通了、流水线通了、NPU 通了。模型部分见下一节。

---

## 12. 模型接入实录：0526_w8a16 的三个"隐藏约定"

拿到 `0526_qcs6490_w8a16.qnn231.ctx.bin.aidem`（INT16 激活）后，头两版都是"能跑但检不出"，
最后定位到 **三处和旧模型不一样的地方**，现在都做成了配置项（不用改代码）。

### 12.1 输入必须喂 float32(0~1)，不能喂 uint8

| 喂法 | 同一帧的最佳置信度 |
| --- | --- |
| `uint8` + `is_native=true`（按模型声明的 dtype 走） | **0.022** ❌ |
| `float32(0~1)` + `is_native=false`（让 SDK 用模型量化参数转换） | **0.371 → 0.92** ✅ |

与 AidLite 文档里"SNPE/QNN 输入输出统一 float32"一致。已在 `backend_aidlite.cpp` 固定：
host 侧永远 `vector<float>`，`set_input_tensor(..., is_native=false)`。

### 12.2 输出是 logits，要在 NMS 前做 sigmoid

新模型把 sigmoid 留在了图外：置信度/颜色/类别通道是 `-58 ~ +20` 的实数。
不处理的话 `conf < 0.3` 永远成立 → 一个框都出不来。

```jsonc
"ApplySigmoid" : true      // 旧模型（图内含 sigmoid）保持 false
```

### 12.3 四点原始顺序和旧模型不一样

旧代码 `nms_get_fp()` 写死了 `{0,6,2,4}` 的重排（适配旧模型的顺序）。
新模型**原始就是 左上-左下-右下-右上**，再重排会得到"蝴蝶结"自交四边形（PnP 必错）。

实测四种映射（自交 = 0）：

| 模式 | 映射 | 结果 |
| --- | --- | --- |
| 0（旧默认） | `{0,6,2,4}` | 自交 ❌ |
| 1 | `{0,4,6,2}` | 自交 ❌ |
| 2 | `{0,6,4,2}` | 凸，但顺序是 左上-右上-右下-左下 ❌ |
| **3** | `{0,2,4,6}` | **凸 + 顺序正确 ✅** |

```jsonc
"PointOrder" : 3
```

### 12.4 验证结果（生产代码路径）

`rm::Infer` + `rm::yoloArmorNMS_FPX` 在 `demo.avi` 上逐帧推理：

```
frame  20: 1 det | best conf=0.898 class=0 box=(746,558 310x158)
frame 120: 1 det | best conf=0.359 class=8 box=(736,419 330x118)
frame 240: 1 det | best conf=0.966 class=0 box=(732,511 249x121)
frame 300: 1 det | best conf=0.982 class=0 box=(717,540 205x92)
```

四点贴合装甲板角点（图见 `docs/img/det_video.jpg`），
`class=0 → ClassMap[0]=0 → ARMOR_ID_SENTRY`，与视频里的哨兵装甲板吻合。

### 12.5 ⚠️ 待确认：颜色通道顺序（ColorMap）

同一帧里**蓝色灯条的装甲板**被判成 `color_id = 0`，而当前 `ColorMap = [1,0,2,3]`
会把 `color_id 0` 映射成 **RED** —— 也就是**红蓝可能是反的**。

* 若训练标签顺序是 `[蓝, 红, 紫, 无]` → `"ColorMap" : [0, 1, 2, 3]`
* 若训练顺序是 `[红, 蓝, 紫, 无]` → 当前 `[1,0,2,3]` 才对，说明模型把蓝色判成了红色

**红蓝反了会打自己人**，必须确认后再上车。

### 12.6 新模型下的配置片段

```jsonc
"FPX" : {
    "DirModel"     : "/etc/openrm/models/0526_qcs6490_w8a16.qnn231.ctx.bin.aidem",
    "Backend"      : "AIDLITE",
    "InferWidth"   : 640,
    "InferHeight"  : 640,
    "LocateNum"    : 8,
    "ColorNum"     : 4,
    "ClassNum"     : 9,
    "BboxesNum"    : 25200,
    "StructNum"    : 22,
    "InputLayout"  : "NHWC",
    "InputType"    : "FLOAT32",
    "ApplySigmoid" : true,
    "PointOrder"   : 3,
    "Framework"    : "QNN",
    "Accelerate"   : "DSP",
    "ConfThresh"   : 0.3,
    "NMSThresh"    : 0.02
}
```


