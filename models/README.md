# models — 导出的 NPU 模型（AIMO → QNN）

两个文件都是 AIMO 平台转出来的 `.ctx.bin.aidem`（AidLite 的加密容器，**设备绑定**，
换板子要重新导出）。放到板上 `/etc/openrm/models/` 即可，代码不用改。

| 文件 | 精度 | 状态 | 说明 |
| --- | --- | --- | --- |
| `0526_qcs6490_w8a16.qnn231.ctx.bin.aidem` | **w8a16（INT16 激活）** | ✅ **在用** | 置信度正常（0.8~0.99），四点精确 |
| `0526_qcs6490_w8a8.qnn231.ctx.bin.aidem` | w8a8（INT8 激活） | ❌ 废弃 | 输出反量化步长 24.6，置信度被量化成 0，永远检不出 |

- `qnn_model_info_w8a16.json`：AIMO 导出的元信息
  ```json
  {"inputDimensions": {"images": [1, 640, 640, 3]},      // NHWC
   "outputDimensions": {"output": [1, 25200, 22]},        // 8 四点 + 1 conf + 4 颜色 + 9 ID
   "data_type": "w8a16", "htp_socs_name": "QCS6490", "qnn_sdk": "2.31.0.250130"}
  ```
- `conversion_log_w8a16.txt`：转换日志

## AIMO 转换参数（复现用）

| 项 | 值 |
| --- | --- |
| 数据精度 | **INT16**（w8a16） |
| 量化模式 | CLE |
| 数据集 | Custom，789 张真实赛场图 |
| 色彩模式 | RGB |
| 均值 / 方差 | `0,0,0` / `255,255,255`（模型预处理就是 `pixel/255`，不要用 ImageNet 那组） |
| 目标平台 | QCS6490 / QNN 2.31 |

> 板上装的是 QNN **2.36** 后端，实测能加载 2.31 转出来的 ctx（init/load 均 rc=0）。

## 与配置的对应关系

这个模型有三处和旧模型不同，都在 `/etc/openrm/Config.json` 里开关：

```jsonc
"FPX" : {
    "DirModel"     : "/etc/openrm/models/0526_qcs6490_w8a16.qnn231.ctx.bin.aidem",
    "InputLayout"  : "NHWC",     // 模型实际是 NHWC（运行时也会自动探测覆盖）
    "InputType"    : "FLOAT32",  // host 侧喂 0~1 float，由 SDK 量化
    "ApplySigmoid" : true,       // 输出是 logits，NMS 前要 sigmoid
    "PointOrder"   : 3,          // 四点原始顺序已是 左上-左下-右下-右上
    "ColorNum"     : 4,
    "ClassNum"     : 9,
    "StructNum"    : 22,
    "BboxesNum"    : 25200
}
```

⚠️ **红蓝映射待确认**：蓝色灯条的板子被判成 `color_id = 0`，当前 `ColorMap = [1,0,2,3]`
会映射成 RED。若训练标签顺序是 `[蓝,红,紫,无]`，应改成 `[0,1,2,3]`。
