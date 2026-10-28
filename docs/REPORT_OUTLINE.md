# 《AidLux 犀牛派 A1 自瞄移植使用报告》大纲

> 素材来源：本仓库 `docs/DEPLOY_AIDLUX.md`（12 节实测记录）、`tools/board/`（全部验证脚本）、
> `docs/img/`（证据图）、`models/README.md`（模型参数）。
> 括号里是该节**已经有的实测数据**，写报告时直接填。

---

## 0. 摘要（1 页）

- 一句话结论：把 TJURM+OpenRM 的 TensorRT 推理换成 AidLite(QNN) 后，在犀牛派 A1 上**跑通全流程**，
  NPU 单帧 3.26 ms、整条流水线 45 fps（1440×1080）。
- 四项关键指标表：推理延迟 / 流水线帧率 / 检出置信度 / 编译与部署耗时
- 结论与遗留问题一句话（红蓝映射待确认、内参待标定）

## 1. 项目背景与目标

- 1.1 原自瞄系统组成：TJURM-2024（框架）+ OpenRM-2024（算法库），原目标平台 Nvidia NX/AGX
- 1.2 为什么要换平台：无 CUDA 可用、成本/功耗/供货
- 1.3 目标：在犀牛派 A1 上达到「可上车」的帧率与精度
- 1.4 验收标准（自定）：≥30 fps、检出率、四点误差、红蓝正确

## 2. 平台与硬件选型

- 2.1 犀牛派 A1 规格（QCS6490 / 12 TOPS INT8 / 8 核 / 7.2 GB / Ubuntu 24.04）
- 2.2 推理栈：AidLite 2.3.2.248 + QNN 2.36（模型按 2.31 导出）
- 2.3 相机：海康 MV-CS016-10UC（1440×1080 / BayerRG8 / USB3 Vision / 60 fps）
- 2.4 与 Jetson 方案对比表（算力、功耗、CUDA、生态、成本）

## 3. 原系统依赖分析（改之前）

- 3.1 依赖清单（OpenCV / Eigen / Ceres / ncurses / CUDA-TensorRT / 工业相机 SDK）
- 3.2 CUDA 到底卡在哪（6 个硬点：CMake REQUIRED、pipeline.h 成员、预处理 kernel、
  detectEnqueue/Output、mallocYoloCameraBuffer、openrm.h 无条件 include tensorrt.h）
- 3.3 最隐蔽的一条：`nms.cpp` 放在 `src/tensorrt/`，无 CUDA 就不编译 → NMS 符号缺失
- 3.4 业务代码是否存在别的隐藏依赖（结论：没有，算法主体纯 CPU）

## 4. 移植方案设计

- 4.1 设计原则：算法零改动、后端可切换、配置化
- 4.2 `rm::Infer` 抽象层（load / setInput / enqueue / synchronize）
- 4.3 三后端结构（AidLite / TensorRT / Null）与编译期自动探测
- 4.4 预处理从 GPU kernel 换成 CPU 的等价性论证（letterbox 几何、114 填充、RGB、/255、
  NCHW/NHWC）→ 为什么 `nms.cpp` 的坐标反算一行不用改
- 4.5 流水线改造：三级线程 + 握手序（消费标志放到 NMS 之后）+ 原子发布引擎
- 4.6 架构图（改造前 / 改造后）

## 5. 关键改造点清单（可做成表格）

| # | 模块 | 改了什么 | 为什么 |
|---|---|---|---|
| 1 | OpenRM CMake | AidLite 自动探测 / CUDA 变可选 | 无 CUDA 也能编 |
| 2 | 新增 infer 模块 | 后端无关推理 + NMS 移入 | 符号永远存在 |
| 3 | NMS | `ApplySigmoid` / `PointOrder` / thread_local / colors_num | 适配新模型、多线程安全 |
| 4 | 相机 | UVC MJPEG、视频回放驱动、FlipMode、空实现 stub | 板上没有工业相机 SDK |
| 5 | 海康驱动 | 3 处 bug 修复 + MVS 3.x API 适配 | 相机能用 |
| 6 | TJURM | 去 CUDA、Config 参数化、imshow 画框、无 DISPLAY 降级 | 能编、能跑、能看 |
| 7 | 单元/工具 | onnx_inspect / prepare_calib / 板上脚本 20+ | 可复现 |

## 6. 环境搭建（离线部署）

- 6.1 板子无外网 → 离线 deb 方案（PC 下载 → adb push → dpkg -i）
- 6.2 依赖清单与版本（29 个 deb：Eigen 3.4 / Ceres 2.2 / ncurses / SuiteSparse）
- 6.3 AidLite 安装与自检（`aid-pkg`、`get_library_version`）
- 6.4 海康 MVS SDK 安装（**必须整套**，见 8.1）
- 6.5 编译踩坑：Ubuntu 24.04 OpenCV/ffmpeg 的 `libavutil` 引用带版本号 OpenCL 符号
  → `-Wl,--allow-shlib-undefined`；ncurses `OK` 宏 → `NCURSES_NOMACROS`
- 6.6 编译结果：OpenRM 12 个 .so + TJURM 33 MB 可执行

## 7. 模型转换（AIMO → QNN）

- 7.1 模型规格：输入 `[1,640,640,3]` / 输出 `[1,25200,22]`（8 四点 + 1 conf + 4 颜色 + 9 ID）
- 7.2 AIMO 参数逐项（INT16 / CLE / Custom 789 张 / RGB / 均值 0 / 方差 255 / QCS6490 / QNN 2.31）
- 7.3 **失败案例：w8a8 为什么不可用**（实测：反量化步长 24.6、conf 恒为 0、原始输出只用 186~219
  34 个灰阶）→ 结论：混合张量不能用 per-tensor INT8
- 7.4 成功案例：w8a16 的实测结果（conf 0.84~0.99）
- 7.5 本模型的三个"隐藏约定"（实测对比表）
  - 输入必须 float32(0~1)：uint8 native 0.022 vs float 0.371
  - 输出是 logits：`ApplySigmoid`
  - 四点原始顺序：四种映射的凸性对比，`PointOrder=3`
- 7.6 板端加载验证（`.aidem` 直接可用、QNN 2.36 能加载 2.31 ctx、设备许可）

## 8. 相机接入

- 8.1 海康 USB3 Vision 不是 UVC（无 /dev/video*）+ **只装主库导致枚举 0** 的坑
  （`dlopen("libMvUsb3vTL.so")`）
- 8.2 MVS SDK 3.2 的 API 差异（`MV_CC_Initialize` 用 dlsym、`ConvertPixelType` vs `_EX`）
- 8.3 OpenRM 海康驱动的两个 bug（`pDeviceInfo` 下标越界、没建 frame buffer）
- 8.4 安装姿态补偿（FlipMode 0/1/2/3）与 A/B 验证（三种翻转命中率一致，说明与翻转无关）
- 8.5 曝光/增益/对焦的实操结论（距离、过曝、镜头盖、光圈）

## 9. 性能测试与数据

- 9.1 单帧拆解：letterbox 5.55 ms + set 0.30 + invoke **3.26** + get 1.46 ≈ 10.3 ms
- 9.2 相机采集：59.2 fps（含 BayerRG8→BGR）
- 9.3 整条流水线：45 fps（22.2 ms/帧，录像回放）/ 32 fps（实机单线程串行）
- 9.4 与 Jetson NX 的对比（原 README 标称 180 fps，但那是 v5n 小模型 + GPU）
- 9.5 瓶颈分析（CPU letterbox 占 54%、反量化 14%、NPU 30%）与优化方向
  （双缓冲/双实例、降分辨率重标内参、CvThreads 调优、MJPEG vs YUYV）

## 10. 可视化与调试手段

- 10.1 无 DISPLAY 时 `-s` 会 abort → 守卫 + 自动降级
- 10.2 三种可视化方案（VNC / SSH X11 转发 / HDMI 直连）+ 内置 MJPEG 流（可选）
- 10.3 VNC 实操（Xtigervnc :1 / 5901 / adb forward / 密码），以及 `setsid` 防会话被杀
- 10.4 imshow 原本不画检测框 → 新增绘制（四点 + 类别 + 置信度）
- 10.5 录像回放当相机（台架复现问题，不用接硬件）
- 10.6 板上自检脚本清单（冒烟 / 逐通道 / 计时 / 相机 / A-B）

## 11. 问题与解决汇总（表格：现象 → 根因 → 解决 → 耗时）

至少包含：
1. `find_package(CUDA REQUIRED)` 编译失败
2. 无 CUDA 时 NMS 符号缺失
3. `openrm.h` 无条件 include Tensorrt
4. Ubuntu 24.04 OpenCL 链接错误
5. ncurses OK 宏冲突
6. 海康相机枚举 0 台
7. MVS 3.2 缺 Initialize/Finalize
8. 海康驱动下标越界 / buffer 未初始化
9. 只喂 uint8 导致置信度 0.02
10. 输出是 logits 导致零检出
11. 四点顺序错 → 自交四边形 / PnP 错
12. w8a8 量化把置信度抹平
13. `-s` 在无显示环境 abort
14. VNC 后台进程被 adb 会话带走
15. 相机全黑（镜头盖/光圈）

## 12. 现存问题与后续计划

- 12.1 ⚠️ 红蓝映射（ColorMap）待确认 —— 蓝板被判 color_id 0，当前映射成 RED，**有打自己人的风险**
- 12.2 ⚠️ 内参用大恒 1440×1080 占位值 → PnP 距离/角度不可信，需棋盘格重标
- 12.3 相机取景/对焦规范（占画面 1/4~1/5）
- 12.4 性能优化路线（双实例流水线、模型输入降尺寸的取舍、NMS CPU 化）
- 12.5 鲁棒性（掉线重连、USB reset、看门狗 —— 参考团队原有 worker 方案）
- 12.6 上车验证清单（串口协议、延时补偿、开火策略、实弹测试）

## 13. 结论

- 移植可行性与达成度（对照 1.4 的验收标准逐条打勾）
- 工程价值（算法零改动、配置化、可回退到 TensorRT）
- 一句话建议

## 附录

- A. 关键配置片段（`/etc/openrm/Config.json` 的 Camera / Model 段）
- B. 板上部署与运行命令（build/deploy/run/可视化）
- C. 脚本清单（用途 + 调用方式）
- D. 名词表（AIMO / AidLite / QNN / HTP / UIB / PTQ / W8A16 …）
- E. 参考链接（AidLux 文档、AIMO、海康 MVS 手册、TJURM/OpenRM 仓库）
