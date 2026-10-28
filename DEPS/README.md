# DEPS — 离线依赖包

板子（AidLux 犀牛派 A1，Ubuntu 24.04 **aarch64**）**没有外网**，所有依赖都是先在 PC 上下载好，
再通过 `adb push` + `dpkg -i` 装进去的。这里就是那份离线包。

> 如果你的板子能上网，直接 `sudo apt install libeigen3-dev libceres-dev libncurses-dev` 即可，
> 不需要这个目录。

## apt-arm64/  —— 29 个 deb（Eigen / Ceres / ncurses 及其依赖）

安装：

```bash
adb push DEPS/apt-arm64 /tmp/debs
adb shell "mv /tmp/debs/apt-arm64/* /tmp/debs/ 2>/dev/null; dpkg -i /tmp/debs/*.deb"
```

| 包 | 版本 | 用途 |
| --- | --- | --- |
| `libeigen3-dev` | 3.4.0-4build0.1 | 卡尔曼 / 坐标变换 |
| `libceres-dev` + `libceres4t64` | 2.2.0+dfsg-4.1ubuntu2 | `ceres::Jet` 自动微分 + 多项式拟合 |
| `libncurses-dev` | 6.4+20240113-1ubuntu2 | `openrm` 参数面板 |
| `libblas-dev` / `liblapack-dev` | 3.12.0-3build1.1 | Ceres 线性代数后端 |
| `libgflags-dev` / `libgoogle-glog-dev` | 2.2.2 / 0.6.0 | Ceres 依赖 |
| `libsuitesparse-dev` 及 14 个子库 | 7.6.1+dfsg-1build1 | Ceres 稀疏求解（CHOLMOD/SPQR…） |
| `libgraphblas-dev` / `libgraphblas7` | 7.4.0+dfsg-1build1 | SuiteSparse 依赖 |

来源：`https://mirrors.aliyun.com/ubuntu-ports/`（noble / arm64）

## hikrobot-sdk-arm64/ —— 海康 MV-CS016-10UC 用的 MVS SDK

**必须整套一起装**，只拷 `libMvCameraControl.so` 会枚举不到相机（它内部
`dlopen("libMvUsb3vTL.so")`，U3V 传输层缺了就静默返回 0 个设备）。

```bash
adb push DEPS/hikrobot-sdk-arm64/lib/. /usr/local/lib/
adb shell "ldconfig"
```

| 文件 | 作用 |
| --- | --- |
| `libMvCameraControl.so` | 主库（**版本 3.2.0.3**：没有 `MV_CC_Initialize/Finalize`，用 `MV_CC_ConvertPixelType` 而不是 `*_EX`） |
| `libMvUsb3vTL.so` | **USB3 Vision 传输层（关键）** |
| `libFormatConversion.so` / `libMediaProcess.so` / `libMVRender.so` | 格式转换 / 图像处理 / 渲染 |

`include/` 里是与该库**版本配套**的头文件，已经同步到
`OpenRM-main/include/video/hikvision/`。

## aidlite/ —— AidLite 头文件

`aidlite.hpp`（AidLite 2.3.2，从板子 `/usr/local/include/aidlux/aidlite/` 拉回来做参考）。
库本身由 `sudo aid-pkg install aidlite-sdk aidlite-qnn236` 提供，不在这里。

**注意命名空间**：公开 API 在 `Aidlux::Aidlite`，不是 `Aidlux`。
