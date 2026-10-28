#include "backend.h"

namespace rm {
namespace detail {

// 没有任何可用后端时的空实现：所有调用失败并给出明确报错，
// 便于在没有 NPU/CUDA 的机器上先跑通编译链路。
class NullBackend : public InferBackend {
public:
    bool load(const std::string& model_path, const InferOptions&) override {
        err_ = "Infer: no available backend for " + model_path +
               " (rebuild with -DOPENRM_WITH_AIDLITE=ON or CUDA/TensorRT)";
        return false;
    }

    void* inputBuffer() override { return nullptr; }
    const BackendInfo& info() const override { return info_; }
    bool invoke() override { return false; }
    const float* outputBuffer() override { return nullptr; }
    const std::string& error() const override { return err_; }

private:
    BackendInfo info_;
    std::string err_;
};

std::unique_ptr<InferBackend> createNullBackend() {
    return std::unique_ptr<InferBackend>(new NullBackend());
}

}
}
