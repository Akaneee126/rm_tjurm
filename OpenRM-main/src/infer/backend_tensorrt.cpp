#include "backend.h"

#include <cstring>
#include <string>
#include <vector>

#ifdef OPENRM_WITH_TENSORRT

#include <cuda_runtime.h>

#include "tensorrt/tensorrt.h"
#include "uniterm/uniterm.h"

#include <unistd.h>

namespace rm {
namespace detail {

// TensorRT 后端：保留原 Jetson / Nvidia 平台的 .engine 路径
class TensorrtBackend : public InferBackend {
public:
    ~TensorrtBackend() override {
        if (input_device_ != nullptr) cudaFree(input_device_);
        if (output_device_ != nullptr) cudaFree(output_device_);
        if (input_host_ != nullptr) cudaFreeHost(input_host_);
        if (output_host_ != nullptr) cudaFreeHost(output_host_);
    }

    bool load(const std::string& model_path, const InferOptions& options) override {
        opt_ = options;
        err_.clear();

        info_.valid             = true;
        info_.input_width       = options.input_width;
        info_.input_height      = options.input_height;
        info_.input_layout      = options.input_layout;
        info_.input_type        = options.input_type;
        info_.output_bboxes_num = options.output_bboxes_num;
        info_.output_struct_num = options.output_struct_num;

        if (options.input_layout != InferLayout::NCHW || options.input_type != InferDType::FLOAT32) {
            err_ = "TensorRT: only NCHW float32 input is supported";
            return false;
        }

        std::string engine_file = model_path;
        // ".engine" 是 7 个字符，别写成 5
        if (engine_file.size() < 7 || engine_file.compare(engine_file.size() - 7, 7, ".engine") != 0) {
            engine_file += ".engine";
        }

        if (access(engine_file.c_str(), F_OK) != 0) {
            if (access(model_path.c_str(), F_OK) != 0) {
                err_ = "TensorRT: no engine and no onnx found: " + model_path;
                return false;
            }
            rm::message("TensorRT: converting ONNX -> engine ...", rm::MSG_NOTE);
            if (!rm::initTrtOnnx(model_path, engine_file, 1U)) {
                err_ = "TensorRT: build engine failed";
                return false;
            }
        }

        if (!rm::initCudaStream(&stream_)) {
            err_ = "TensorRT: create cuda stream failed";
            return false;
        }

        if (!rm::initTrtEngine(input_name_, output_name_, engine_file, &context_)) {
            err_ = "TensorRT: init engine failed";
            return false;
        }

        const size_t input_bytes = static_cast<size_t>(opt_.input_width) *
                                   static_cast<size_t>(opt_.input_height) * 3u * sizeof(float);
        const size_t output_bytes = static_cast<size_t>(opt_.output_bboxes_num) *
                                    static_cast<size_t>(opt_.output_struct_num) * sizeof(float);

        cudaMallocHost(reinterpret_cast<void**>(&input_host_), input_bytes);
        cudaMalloc(reinterpret_cast<void**>(&input_device_), input_bytes);
        cudaMalloc(reinterpret_cast<void**>(&output_device_), output_bytes);
        cudaMallocHost(reinterpret_cast<void**>(&output_host_), output_bytes);

        if (input_host_ == nullptr || input_device_ == nullptr ||
            output_device_ == nullptr || output_host_ == nullptr) {
            err_ = "TensorRT: cuda buffer allocation failed";
            return false;
        }

        return true;
    }

    void* inputBuffer() override { return input_host_; }

    const BackendInfo& info() const override { return info_; }

    bool invoke() override {
        if (context_ == nullptr) {
            err_ = "TensorRT: invoke before load";
            return false;
        }

        const size_t input_bytes = static_cast<size_t>(opt_.input_width) *
                                   static_cast<size_t>(opt_.input_height) * 3u * sizeof(float);

        cudaMemcpyAsync(input_device_, input_host_, input_bytes,
                        cudaMemcpyHostToDevice, stream_);

        rm::detectEnqueue(
            input_name_,
            output_name_,
            input_device_,
            output_device_,
            &context_,
            &stream_);

        const size_t output_bytes = static_cast<size_t>(opt_.output_bboxes_num) *
                                    static_cast<size_t>(opt_.output_struct_num) * sizeof(float);

        cudaMemcpyAsync(output_host_, output_device_, output_bytes,
                        cudaMemcpyDeviceToHost, stream_);
        cudaStreamSynchronize(stream_);
        return true;
    }

    const float* outputBuffer() override { return output_host_; }

    const std::string& error() const override { return err_; }

private:
    InferOptions                opt_;
    BackendInfo                 info_;
    std::string                 err_;

    const char*                 input_name_  = "images";
    const char*                 output_name_ = "output";

    cudaStream_t                stream_         = nullptr;
    nvinfer1::IExecutionContext* context_       = nullptr;
    float*                      input_host_     = nullptr;
    float*                      input_device_   = nullptr;
    float*                      output_device_  = nullptr;
    float*                      output_host_    = nullptr;
};

std::unique_ptr<InferBackend> createTensorrtBackend() {
    return std::unique_ptr<InferBackend>(new TensorrtBackend());
}

}
}

#else  // !OPENRM_WITH_TENSORRT

namespace rm {
namespace detail {

std::unique_ptr<InferBackend> createTensorrtBackend() {
    return nullptr;
}

}
}

#endif
