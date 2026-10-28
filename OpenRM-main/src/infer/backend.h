#ifndef __OPENRM_INFER_BACKEND_H__
#define __OPENRM_INFER_BACKEND_H__

#include <memory>
#include <string>

#include "infer/infer.h"

namespace rm {
namespace detail {

// 后端自报的输入输出信息（用于与配置文件交叉校验）
struct BackendInfo {
    bool        valid             = false;
    int         input_width       = 0;
    int         input_height      = 0;
    InferLayout input_layout      = InferLayout::NCHW;
    InferDType  input_type        = InferDType::FLOAT32;
    int         output_bboxes_num = 0;
    int         output_struct_num = 0;
};

class InferBackend {
public:
    virtual ~InferBackend() = default;

    virtual bool load(const std::string& model_path, const InferOptions& options) = 0;

    // letterbox 结果写入的 host 张量首地址
    virtual void* inputBuffer() = 0;

    virtual const BackendInfo& info() const = 0;

    // 同步推理一次
    virtual bool invoke() = 0;

    // invoke 成功后有效，host 侧输出首地址
    virtual const float* outputBuffer() = 0;

    virtual const std::string& error() const = 0;
};

std::unique_ptr<InferBackend> createAidliteBackend();
std::unique_ptr<InferBackend> createTensorrtBackend();
std::unique_ptr<InferBackend> createNullBackend();

}
}

#endif
