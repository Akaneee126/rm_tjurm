#include "backend.h"

#include <cctype>
#include <cstring>
#include <string>
#include <vector>

#ifdef OPENRM_WITH_AIDLITE

#include <aidlux/aidlite/aidlite.hpp>

#include "uniterm/uniterm.h"

// AidLite 的公开 API 在 Aidlux::Aidlite 命名空间里（不是 Aidlux）
using namespace Aidlux::Aidlite;

namespace {

// AidLite 的枚举值在官方头文件里是固定的，这里统一用手写整型常量 + static_cast。
// 原因：不同 SDK 版本的 aidlite.hpp 枚举成员未必齐全（例如有些版本没有
// TYPE_NPU / TYPE_QNN248），直接用名字会在某些版本上编译失败。
// 值来源：/usr/local/include/aidlux/aidlite/aidlite.hpp（2.3.2）
constexpr int kFwSnpe    = 1;
constexpr int kFwTflite  = 2;
constexpr int kFwQnn     = 4;
constexpr int kFwSnpe2   = 5;
constexpr int kFwMnn     = 7;
constexpr int kFwOnnx    = 11;

constexpr int kAccCpu    = 1;
constexpr int kAccGpu    = 2;
constexpr int kAccDsp    = 3;   // 高通平台的 DSP 即 HTP(NPU)
constexpr int kAccNpu    = 4;

constexpr int kDtUint8   = 1;
constexpr int kDtInt8    = 2;
constexpr int kDtFloat32 = 4;

constexpr int kImplLocal = 3;

std::string upperOf(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (char c : s) o.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    return o;
}

FrameworkType frameworkTypeOf(const std::string& name) {
    struct Entry {
        const char* tag;
        int         value;
    };
    static const Entry kTable[] = {
        {"QNN216", 101}, {"QNN223", 103}, {"QNN229", 105}, {"QNN231", 107},
        {"QNN236", 108}, {"QNN240", 109}, {"QNN248", 110},
        {"SNPE216", 102}, {"SNPE223", 104}, {"SNPE229", 106},
    };

    const std::string upper = upperOf(name);
    for (const auto& e : kTable) {
        if (upper == e.tag) return static_cast<FrameworkType>(e.value);
    }
    if (upper == "SNPE" || upper == "SNPE2") return static_cast<FrameworkType>(kFwSnpe2);
    if (upper == "ONNX")                     return static_cast<FrameworkType>(kFwOnnx);
    if (upper == "TFLITE")                   return static_cast<FrameworkType>(kFwTflite);
    if (upper == "MNN")                      return static_cast<FrameworkType>(kFwMnn);
    return static_cast<FrameworkType>(kFwQnn);
}

AccelerateType accelerateTypeOf(const std::string& name) {
    const std::string upper = upperOf(name);
    if (upper == "NPU") return static_cast<AccelerateType>(kAccNpu);
    if (upper == "CPU") return static_cast<AccelerateType>(kAccCpu);
    if (upper == "GPU") return static_cast<AccelerateType>(kAccGpu);
    return static_cast<AccelerateType>(kAccDsp);
}

DataType dataTypeOf(rm::InferDType type) {
    switch (type) {
        case rm::InferDType::UINT8: return static_cast<DataType>(kDtUint8);
        case rm::InferDType::INT8:  return static_cast<DataType>(kDtInt8);
        case rm::InferDType::FLOAT32:
        default:                    return static_cast<DataType>(kDtFloat32);
    }
}

// framework="QNN" + qnn_version="2.31"  ->  "QNN231"
std::string frameworkTag(const rm::InferOptions& opt) {
    if (opt.qnn_version.empty()) return opt.framework;

    std::string digits;
    for (char c : opt.qnn_version) {
        if (std::isdigit(static_cast<unsigned char>(c))) digits.push_back(c);
    }
    if (digits.empty()) return opt.framework;

    const std::string upper = upperOf(opt.framework);
    if (upper == "QNN")  return "QNN" + digits;
    if (upper == "SNPE") return "SNPE" + digits;
    return opt.framework;
}

}  // namespace

namespace rm {
namespace detail {

class AidliteBackend : public InferBackend {
public:
    ~AidliteBackend() override = default;

    bool load(const std::string& model_path, const InferOptions& options) override {
        opt_ = options;
        err_.clear();

        // 先用配置值兜底，探测成功后再用模型真实信息覆盖
        info_.valid             = false;
        info_.input_width       = options.input_width;
        info_.input_height      = options.input_height;
        info_.input_layout      = options.input_layout;
        info_.input_type        = options.input_type;
        info_.output_bboxes_num = options.output_bboxes_num;
        info_.output_struct_num = options.output_struct_num;

        model_ = Model::create_instance(model_path.c_str());
        if (model_ == nullptr) {
            err_ = "AidLite: create model instance failed: " + model_path;
            return false;
        }

        // 输入/输出 shape 用配置值声明（LOCAL 模式下 SDK 以模型自身为准）
        std::vector<std::vector<uint32_t>> input_shapes;
        if (options.input_layout == InferLayout::NHWC) {
            input_shapes = {{1u,
                             static_cast<uint32_t>(options.input_height),
                             static_cast<uint32_t>(options.input_width),
                             3u}};
        } else {
            input_shapes = {{1u,
                             3u,
                             static_cast<uint32_t>(options.input_height),
                             static_cast<uint32_t>(options.input_width)}};
        }
        std::vector<std::vector<uint32_t>> output_shapes = {
            {1u,
             static_cast<uint32_t>(options.output_bboxes_num),
             static_cast<uint32_t>(options.output_struct_num)}};

        const int rc = model_->set_model_properties(
            input_shapes, dataTypeOf(options.input_type),
            output_shapes, static_cast<DataType>(kDtFloat32));
        if (rc != 0) {
            // LOCAL 模式下这一步不是必须的，模型会用自己的 shape，继续即可
            rm::message("AidLite: set_model_properties returned " + std::to_string(rc) +
                        " (ignored, using model's own shapes)", rm::MSG_WARNING);
        }

        config_ = Config::create_instance();
        if (config_ == nullptr) {
            err_ = "AidLite: create config failed";
            return false;
        }

        config_->framework_type  = frameworkTypeOf(frameworkTag(options));
        config_->accelerate_type = accelerateTypeOf(options.accelerate);
        config_->implement_type  = static_cast<ImplementType>(kImplLocal);
        config_->is_quantify_model = 0;
        if (options.threads > 0) config_->number_of_threads = static_cast<int8_t>(options.threads);
        if (!options.backend_extension_config.empty()) {
            config_->backend_extension_config = options.backend_extension_config;
        }

        interpreter_ = InterpreterBuilder::build_interpreter_from_model_and_config(model_, config_);
        if (interpreter_ == nullptr) {
            err_ = "AidLite: build_interpreter_from_model_and_config failed";
            return false;
        }

        if (interpreter_->init() != 0) {
            err_ = "AidLite: interpreter init failed";
            return false;
        }
        if (interpreter_->load_model() != 0) {
            err_ = "AidLite: interpreter load_model failed";
            return false;
        }

        // 查询模型真实的 IO（布局/尺寸/数据类型），失败则沿用配置
        probeTensorInfo();

        allocateBuffers();
        return true;
    }

    void* inputBuffer() override { return input_.data(); }

    const BackendInfo& info() const override { return info_; }

    bool invoke() override {
        if (interpreter_ == nullptr) {
            err_ = "AidLite: invoke before load";
            return false;
        }

        // host 侧输入恒为 float32 → is_native=false，让 SDK 做量化
        int rc = interpreter_->set_input_tensor(0, input_.data(), false);
        if (rc != 0) {
            err_ = "AidLite: set_input_tensor failed (rc=" + std::to_string(rc) + ")";
            return false;
        }

        rc = interpreter_->invoke();
        if (rc != 0) {
            err_ = "AidLite: invoke failed (rc=" + std::to_string(rc) + ")";
            return false;
        }

        void* out = nullptr;
        uint32_t out_len = 0;
        rc = interpreter_->get_output_tensor(0, &out, &out_len);
        if (rc != 0 || out == nullptr) {
            err_ = "AidLite: get_output_tensor failed (rc=" + std::to_string(rc) + ")";
            return false;
        }

        const size_t want = output_.size() * sizeof(float);
        const size_t have = static_cast<size_t>(out_len);
        if (have != 0 && have < want) {
            err_ = "AidLite: output too small: got " + std::to_string(have) +
                   " bytes, expect " + std::to_string(want);
            return false;
        }
        std::memcpy(output_.data(), out, want);
        return true;
    }

    const float* outputBuffer() override { return output_.data(); }

    const std::string& error() const override { return err_; }

private:
    // 查询模型真实输入/输出信息；成功则填充 info_ 并置 valid
    void probeTensorInfo() {
        std::vector<std::vector<TensorInfo>> in_info;
        std::vector<std::vector<TensorInfo>> out_info;

        if (interpreter_->get_input_tensor_info(in_info) != 0 || in_info.empty() || in_info[0].empty()) {
            rm::message("AidLite: get_input_tensor_info failed, using config values", rm::MSG_WARNING);
            return;
        }
        const TensorInfo& t = in_info[0][0];

        if (t.shape.size() == 4) {
            const uint64_t d1 = t.shape[1];
            const uint64_t d2 = t.shape[2];
            const uint64_t d3 = t.shape[3];
            if (d1 == 3 && d3 != 3) {
                info_.input_layout = InferLayout::NCHW;
                info_.input_height = static_cast<int>(d2);
                info_.input_width  = static_cast<int>(d3);
            } else if (d3 == 3) {
                info_.input_layout = InferLayout::NHWC;
                info_.input_height = static_cast<int>(d1);
                info_.input_width  = static_cast<int>(d2);
            }
        }

        // 模型声明的是 uint8/int8，但按 QNN 官方约定，host 侧统一喂 float32(0~1)，
        // 由 SDK 用模型自身的量化参数转换。实测这条路径才和训练一致：
        // 同一帧喂 uint8(native) 置信度只有 0.02，喂 float 能到 0.37 且框正好压在装甲板上。
        probe_input_type_ = static_cast<int>(t.element_type);
        info_.input_type = InferDType::FLOAT32;

        if (interpreter_->get_output_tensor_info(out_info) == 0 &&
            !out_info.empty() && !out_info[0].empty()) {
            const TensorInfo& o = out_info[0][0];
            if (o.shape.size() == 3) {
                info_.output_bboxes_num = static_cast<int>(o.shape[1]);
                info_.output_struct_num = static_cast<int>(o.shape[2]);
            }
        }

        info_.valid = true;

        rm::message(
            "AidLite model IO: input " +
                std::string(info_.input_layout == InferLayout::NHWC ? "NHWC " : "NCHW ") +
                std::to_string(info_.input_width) + "x" + std::to_string(info_.input_height) +
                " (model dtype id " + std::to_string(probe_input_type_) +
                ", fed as float32), output " +
                std::to_string(info_.output_bboxes_num) + "x" +
                std::to_string(info_.output_struct_num),
            rm::MSG_OK);
    }

    void allocateBuffers() {
        const size_t pixels = static_cast<size_t>(info_.input_width) *
                              static_cast<size_t>(info_.input_height);
        // host 侧永远是 float32，量化交给 SDK
        input_.assign(pixels * 3u * sizeof(float), 0);

        const size_t out_elems = static_cast<size_t>(info_.output_bboxes_num) *
                                 static_cast<size_t>(info_.output_struct_num);
        output_.assign(out_elems, 0.0f);
    }

    InferOptions                opt_;
    BackendInfo                 info_;
    std::string                 err_;
    std::vector<uint8_t>        input_;
    std::vector<float>          output_;
    int                         probe_input_type_ = 4;   // AidLite DataType id，仅用于日志

    Model*                      model_ = nullptr;
    Config*                     config_ = nullptr;
    std::unique_ptr<Interpreter> interpreter_;
};

std::unique_ptr<InferBackend> createAidliteBackend() {
    return std::unique_ptr<InferBackend>(new AidliteBackend());
}

}
}

#else  // !OPENRM_WITH_AIDLITE

namespace rm {
namespace detail {

std::unique_ptr<InferBackend> createAidliteBackend() {
    return nullptr;
}

}
}

#endif
