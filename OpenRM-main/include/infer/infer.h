#ifndef __OPENRM_INFER_INFER_H__
#define __OPENRM_INFER_INFER_H__

#include <cstdint>
#include <memory>
#include <string>

#include <opencv2/opencv.hpp>

namespace rm {

// 推理后端
// AUTO 会根据模型后缀和编译期可用后端自动选择
enum class InferBackend {
    AUTO      = 0,
    TENSORRT  = 1,      // Nvidia TensorRT (.engine)
    AIDLITE   = 2,      // AidLux AidLite (Qualcomm QNN .ctx.bin / SNPE .dlc / tflite / onnx)
    NONE      = 3       // 空实现，仅用于编译链路自检
};

// 模型输入张量布局
enum class InferLayout {
    NCHW = 0,
    NHWC = 1
};

// 模型输入张量数据类型
// 注意：AidLite 的 SNPE/QNN 后端在文档中约定输入输出统一为 float32，
// 这里保留 UINT8/INT8 是为了兼容 TFLite/ONNX 后端的量化模型。
enum class InferDType {
    FLOAT32 = 0,
    UINT8   = 1,
    INT8    = 2
};

struct InferOptions {
    // ---- 输入 ----
    int         input_width      = 640;
    int         input_height     = 640;
    InferLayout input_layout     = InferLayout::NCHW;
    InferDType  input_type       = InferDType::FLOAT32;
    float       pad_value        = 114.0f;   // letterbox 填充值，与训练一致
    bool        keep_aspect_ratio = true;    // false 时直接拉伸

    // ---- 输出 ----
    // 输出被视作 output_bboxes_num 个长度为 output_struct_num 的 float 记录
    int         output_bboxes_num = 25200;
    int         output_struct_num = 22;      // locate + conf + color + class

    // ---- 后端 ----
    InferBackend backend = InferBackend::AUTO;

    // AidLite 专用
    std::string framework     = "QNN";       // QNN | SNPE | ONNX | TFLITE
    std::string accelerate    = "DSP";       // NPU | DSP | GPU | CPU
    std::string qnn_version   = "";          // "2.16" "2.23" "2.31" ... 留空自动
    std::string backend_extension_config = "";   // QNN DSP 的 htp backend json
    int         threads       = 2;
};

// 与后端无关的推理引擎封装
//
//   Infer infer;
//   infer.load(model_path, options);
//   infer.setInput(bgr_mat);     // letterbox + 归一化 + 布局变换，纯 CPU/OpenCV
//   infer.enqueue();             // 异步启动一次推理（内部工作线程）
//   const float* out = infer.synchronize();   // 阻塞取回输出（host 内存）
//
// 这样流水线的“预处理器线程负责入队、检测器线程负责取结果”结构保持不变，
// 只是把 CUDA/TensorRT 换成了 AidLite(NPU) 或者仍然可用的 TensorRT。
class Infer {
public:
    Infer();
    ~Infer();

    Infer(const Infer&) = delete;
    Infer& operator=(const Infer&) = delete;

    bool load(const std::string& model_path, const InferOptions& options);

    // 将 BGR8 图像 letterbox 到模型输入张量（RGB、归一化、NCHW/NHWC）
    bool setInput(const cv::Mat& bgr);

    // 异步启动推理，立即返回
    void enqueue();

    // 等待推理结束并返回 host 侧输出首地址
    // 长度为 outputBboxesNum() * outputStructNum() 个 float
    const float* synchronize();

    // 便捷组合：setInput + enqueue + synchronize
    const float* detect(const cv::Mat& bgr);

    bool loaded() const;
    int  outputBboxesNum() const;
    int  outputStructNum() const;
    const std::string& lastError() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// 将 BGR8 图像 letterbox 到目标张量，供其它模块复用
bool letterboxToTensor(
    const cv::Mat& bgr,
    void* dst,
    int dst_width,
    int dst_height,
    InferLayout layout,
    InferDType type,
    float pad_value = 114.0f,
    bool keep_aspect_ratio = true);

const char* inferBackendName(InferBackend backend);

// 配置字符串 -> 枚举
InferBackend inferBackendFromString(const std::string& s);
InferLayout  inferLayoutFromString(const std::string& s);
InferDType   inferDTypeFromString(const std::string& s);

}

#endif
