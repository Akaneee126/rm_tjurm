#ifndef __OPENRM_INFER_NMS_H__
#define __OPENRM_INFER_NMS_H__

// YOLO 系列输出的纯 CPU 后处理（NMS + 坐标反算）。
//
// 这里原来放在 tensorrt/ 目录下，但代码本身完全不依赖 TensorRT/CUDA，
// 放在那里会导致"没有 Nvidia 硬件 → tensorrt 模块不编译 → NMS 符号缺失"。
// 现在它属于与后端无关的推理层，随 openrm_infer 一起编译。

#include <vector>

#include "structure/stamp.hpp"

namespace rm {

// 有些导出（例如我们的 0526 ONNX）把 sigmoid 留在了图外，输出的是 logits：
// 置信度/颜色/类别通道可能是 -20~+20 的实数。打开这个开关后，NMS 会先对这些
// 通道做 sigmoid 再比较阈值。默认关闭，保持"模型内已含 sigmoid"的老模型行为。
void yoloSetSigmoid(bool enable);

// 四点顺序：0 = 旧格式（默认），1 = 行优先（左上-右上-左下-右下 导出）
void yoloSetPointOrder(int mode);

// 标准 yolov5 输出： (x + y + w + h) + conf + n * classes
std::vector<YoloRect> yoloArmorNMS_V5(
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

// 三点/四点输出： (x0 + y0 + ... + x3 + y3) + conf + n * classes
std::vector<YoloRect> yoloArmorNMS_FP(
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

// 四点 + 颜色与 ID 分离：
//   (x0 + y0 + ... + x3 + y3) + conf + m * color_classes + n * id_classes
std::vector<YoloRect> yoloArmorNMS_FPX(
    float* output_host_buffer,
    int output_bboxes_num,
    int classes_num,
    float confidence_threshold,
    float nms_threshold,
    int input_width,
    int input_height,
    int infer_width,
    int infer_height,
    int colors_num = 3
);

// 36 类老格式
std::vector<YoloRect> yoloArmorNMS_V5C36(
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

}

#endif
