# TJURM + OpenRM 的 NPU 移植（AidLux 犀牛派 A1）

把北洋机甲 2024 自瞄（**TJURM 框架 + OpenRM 算法库**）从 **Nvidia CUDA / TensorRT**
移植到 **Qualcomm QCS6490 的 NPU（Hexagon HTP，AidLite + QNN）**，在 **AidLux 犀牛派 A1** 上跑通。

> 板子实测：Ubuntu 24.04 / aarch64 / 8 核；相机 **海康 MV-CS016-10UC（1440×1080）**；
> NPU 推理 **3.26 ms/帧**，整条流水线 **45 fps**（1440×1080 输入）。

---

## 目录

```
.
├── OpenRM-main/          # 算法库（已改：CUDA → 与后端无关的 rm::Infer；海康驱动修复）
├── TJURM-main/           # 自瞄框架（已改：去掉 CUDA 依赖，推理走 rm::Infer）
├── DEPS/                 # 离线依赖（板子没外网，全部走 adb + dpkg）
│   ├── apt-arm64/        #   Eigen / Ceres / ncurses 及其依赖，29 个 deb
│   ├── hikrobot-sdk-arm64/  # 海康 MVS SDK（含关键的 libMvUsb3vTL.so）
│   └── aidlite/          #   AidLite 头文件（参考）
├── models/               # AIMO 导出的模型（w8a16 在用 / w8a8 废弃）
├── docs/
│   └── DEPLOY_AIDLUX.md  # ★ 完整部署文档：依赖、AIMO 转模型、配置、踩坑、实测数据
├── tools/                # 脚本
│   ├── onnx_inspect.py   #   ONNX IO / 算子分布自查
│   ├── prepare_calib.py  #   校准集 letterbox
│   └── board/            #   板上部署 / 自检 / 性能测试脚本
└── 0526_save_path.zip    # AIMO 原始导出包
```

## 改了什么（核心）

| 原来（CUDA / TensorRT） | 现在 |
| --- | --- |
| `memcpyYoloCameraBuffer` + CUDA `resize` 核函数 | `rm::Infer::setInput()`：CPU/OpenCV letterbox（几何与原 kernel 一致） |
| `detectEnqueue` / `detectOutput` | `rm::Infer::enqueue()` / `synchronize()` |
| `initTrtOnnx` / `initTrtEngine` | `rm::Infer::load()`，编译期自动选后端（AidLite / TensorRT / Null） |
| TJURM `find_package(CUDA REQUIRED)` | 去掉；不再依赖 CUDA |
| `nms.cpp` 放在 `src/tensorrt/`（无 CUDA 就不编译 → 链接失败） | 移到 `src/infer/`，永远参与编译 |
| 工业相机 SDK 缺失就链接失败 | 补了空实现（`src/video/stub_camera.cpp`） |
| 海康驱动 3 个 bug | `pDeviceInfo` 1-based 越界、没建 `frame buffer`、SDK 3.x API 不匹配 |

新增能力（都是为了这块板子）：
* `rm::Infer`：与后端无关的推理封装（自动探测输入 layout / dtype）
* UVC MJPEG 采集（`openUVCExt`）、**视频文件当相机**（台架调试）
* NMS 支持 `ApplySigmoid`（模型输出是 logits 时）和 `PointOrder`（四点顺序）
* 配置化：`Backend / DirModel / InputLayout / InputType / ApplySigmoid / PointOrder / Camera.Driver`

## 快速上手

```bash
# 1）板上装依赖（离线）
adb push DEPS/apt-arm64 /tmp/debs
adb shell "mv /tmp/debs/apt-arm64/* /tmp/debs/; dpkg -i /tmp/debs/*.deb"

# 2）海康 SDK
adb push DEPS/hikrobot-sdk-arm64/lib/. /usr/local/lib/
adb shell "ldconfig"

# 3）编译安装
adb push OpenRM-main /root/ && adb push TJURM-main /root/
adb shell "sh /root/OpenRM-main/run.sh"      # 或见 docs 里的 build_openrm.sh
adb shell "cd /root/TJURM-main && sudo ./run.sh -b"

# 4）模型 + 参数
adb push models/0526_qcs6490_w8a16.qnn231.ctx.bin.aidem /etc/openrm/models/
adb push TJURM-main/data/uniconfig/* /etc/openrm/
```

完整步骤、AIMO 怎么转、每个参数什么意思、踩过的坑 → **[docs/DEPLOY_AIDLUX.md](docs/DEPLOY_AIDLUX.md)**

## 当前状态

| 项 | 状态 |
| --- | --- |
| OpenRM / TJURM 在 A1 上编译 | ✅ |
| AidLite(QNN) 加载 `.aidem`、NPU 推理 | ✅ 3.26 ms/帧 |
| 海康 MV-CS016-10UC 取流 | ✅ 1440×1080 @ 60fps |
| 整条流水线（相机→NPU→NMS） | ✅ 45 fps |
| w8a16 模型检测精度 | ✅ 置信度 0.84~0.99，四点贴合（见 docs 第 12 节） |
| 红蓝颜色映射 | ⚠️ 待确认（`ColorMap`） |
| 相机内参 | ⚠️ 目前复用大恒 1440×1080 的占位值，需重标 |

## 说明

* 依赖的离线包在 `DEPS/`，**不进 git**（`.gitignore` 已排除），只保留说明文档。
* `models/` 里的 `.aidem` 是**设备绑定**的加密模型，换板子要用 AIMO 重新导出。
