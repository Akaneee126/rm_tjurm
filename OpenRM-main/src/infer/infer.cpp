#include "infer/infer.h"
#include "backend.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#include <unistd.h>

#include "uniterm/uniterm.h"

using namespace rm;

namespace {

// 每帧复用，避免重复分配 640x640x3 级别的临时图像
struct LetterboxScratch {
    cv::Mat              resized;
    cv::Mat              canvas;      // CV_8UC3，letterbox 画布
    cv::Mat              rgb;         // CV_8UC3，RGB
    cv::Mat              f32;         // CV_32FC3，归一化后
    std::vector<cv::Mat> planes;      // f32 的三通道

    void ensure(int dw, int dh) {
        if (canvas.rows != dh || canvas.cols != dw) {
            canvas.create(dh, dw, CV_8UC3);
            rgb.create(dh, dw, CV_8UC3);
            f32.create(dh, dw, CV_32FC3);
            planes.resize(3);
        }
    }
};

LetterboxScratch& scratch() {
    static thread_local LetterboxScratch s;
    return s;
}

// 把 BGR8 图像写入一个 CHW/NHWC、float32/uint8/int8 的张量
bool writeTensor(
    const cv::Mat& bgr,
    void* dst,
    int dw,
    int dh,
    InferLayout layout,
    InferDType type,
    float pad,
    bool keep_ratio
) {
    if (dst == nullptr) return false;
    if (bgr.empty() || bgr.type() != CV_8UC3) return false;
    if (dw <= 0 || dh <= 0) return false;

    const int sw_src = bgr.cols;
    const int sh_src = bgr.rows;

    int sw = dw;
    int sh = dh;
    if (keep_ratio) {
        const float scale = std::min(static_cast<float>(dw) / sw_src,
                                     static_cast<float>(dh) / sh_src);
        sw = std::max(1, static_cast<int>(std::lround(sw_src * scale)));
        sh = std::max(1, static_cast<int>(std::lround(sh_src * scale)));
    }
    const int pad_x = (dw - sw) / 2;
    const int pad_y = (dh - sh) / 2;

    auto& s = scratch();
    s.ensure(dw, dh);

    if (sw == sw_src && sh == sh_src) {
        bgr.copyTo(s.resized);
    } else {
        cv::resize(bgr, s.resized, cv::Size(sw, sh), 0, 0, cv::INTER_LINEAR);
    }

    // 画布 + 居中贴图（keep_ratio 时上下或左右留灰边，与 CUDA 版 affine 一致）
    s.canvas.setTo(cv::Scalar::all(pad));
    s.resized.copyTo(s.canvas(cv::Rect(pad_x, pad_y, sw, sh)));

    // BGR -> RGB
    cv::cvtColor(s.canvas, s.rgb, cv::COLOR_BGR2RGB);

    const size_t plane_bytes = static_cast<size_t>(dw) * static_cast<size_t>(dh);

    switch (type) {
        case InferDType::UINT8:
        case InferDType::INT8: {
            uint8_t* out = static_cast<uint8_t*>(dst);
            if (layout == InferLayout::NHWC) {
                std::memcpy(out, s.rgb.data, plane_bytes * 3);
            } else {
                for (int c = 0; c < 3; ++c) {
                    const uint8_t* src_plane = s.rgb.data + c;
                    uint8_t* dst_plane = out + plane_bytes * static_cast<size_t>(c);
                    for (size_t i = 0; i < plane_bytes; ++i) {
                        dst_plane[i] = src_plane[i * 3];
                    }
                }
            }
            return true;
        }

        case InferDType::FLOAT32:
        default: {
            s.rgb.convertTo(s.f32, CV_32F, 1.0 / 255.0);
            if (layout == InferLayout::NHWC) {
                // interleaved RGB float，s.f32 是连续内存
                std::memcpy(dst, s.f32.data, plane_bytes * 3 * sizeof(float));
            } else {
                cv::split(s.f32, s.planes);
                float* out = static_cast<float*>(dst);
                for (int c = 0; c < 3; ++c) {
                    std::memcpy(out + plane_bytes * static_cast<size_t>(c),
                                s.planes[c].ptr<float>(),
                                plane_bytes * sizeof(float));
                }
            }
            return true;
        }
    }
}

InferBackend backendFromString(const std::string& s) {
    std::string v;
    v.reserve(s.size());
    for (char c : s) v.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    if (v == "TENSORRT" || v == "TRT") return InferBackend::TENSORRT;
    if (v == "AIDLITE")                 return InferBackend::AIDLITE;
    if (v == "NONE")                    return InferBackend::NONE;
    return InferBackend::AUTO;
}

InferLayout layoutFromString(const std::string& s) {
    return (s == "NHWC" || s == "nhwc") ? InferLayout::NHWC : InferLayout::NCHW;
}

InferDType dtypeFromString(const std::string& s) {
    if (s == "UINT8" || s == "uint8")     return InferDType::UINT8;
    if (s == "INT8" || s == "int8")       return InferDType::INT8;
    return InferDType::FLOAT32;
}

bool endsWith(const std::string& s, const std::string& tail) {
    if (tail.size() > s.size()) return false;
    return std::equal(tail.rbegin(), tail.rend(), s.rbegin());
}

}  // namespace

const char* rm::inferBackendName(InferBackend backend) {
    switch (backend) {
        case InferBackend::TENSORRT: return "TensorRT";
        case InferBackend::AIDLITE:  return "AidLite";
        case InferBackend::NONE:     return "None";
        default:                     return "Auto";
    }
}

bool rm::letterboxToTensor(
    const cv::Mat& bgr,
    void* dst,
    int dst_width,
    int dst_height,
    InferLayout layout,
    InferDType type,
    float pad_value,
    bool keep_aspect_ratio
) {
    return writeTensor(bgr, dst, dst_width, dst_height, layout, type, pad_value, keep_aspect_ratio);
}

// ---------------------------------------------------------------------------

struct Infer::Impl {
    InferOptions   opt;
    std::string    model_path;
    std::string    err;
    bool           is_loaded = false;

    std::unique_ptr<detail::InferBackend> backend;

    // 异步推理工作线程
    std::thread             worker;
    std::mutex              mtx;
    std::condition_variable cv_job;
    std::condition_variable cv_done;
    bool                    has_job   = false;
    bool                    job_done  = true;
    bool                    stop      = false;
    bool                    job_failed = false;

    void loop() {
        for (;;) {
            std::unique_lock<std::mutex> lock(mtx);
            cv_job.wait(lock, [this] { return has_job || stop; });
            if (stop) return;
            has_job = false;
            lock.unlock();

            const bool ok = backend->invoke();

            lock.lock();
            job_failed = !ok;
            job_done = true;
            cv_done.notify_all();
        }
    }

    void start() {
        if (worker.joinable()) worker.join();
        worker = std::thread(&Impl::loop, this);
    }

    void shutdown() {
        if (!worker.joinable()) return;
        {
            std::unique_lock<std::mutex> lock(mtx);
            stop = true;
            cv_job.notify_all();
        }
        worker.join();
    }
};

Infer::Infer() : impl_(new Impl()) {}

Infer::~Infer() {
    if (!impl_) return;
    impl_->shutdown();
}

bool Infer::load(const std::string& model_path, const InferOptions& options) {
    impl_->opt = options;
    impl_->model_path = model_path;
    impl_->err.clear();

    // 允许重复 load()：先把上一次的工作线程停掉，否则重新赋值 std::thread
    // 会在 joinable 状态下调用 std::terminate
    impl_->is_loaded = false;
    impl_->shutdown();
    {
        std::unique_lock<std::mutex> lock(impl_->mtx);
        impl_->has_job = false;
        impl_->job_done = true;
        impl_->job_failed = false;
        impl_->stop = false;
    }
    impl_->backend.reset();

    if (model_path.empty() || access(model_path.c_str(), R_OK) != 0) {
        impl_->err = "infer: model file not found or not readable: " + model_path;
        rm::message(impl_->err, rm::MSG_ERROR);
        return false;
    }

    InferBackend backend = options.backend;
    if (backend == InferBackend::AUTO) {
        // 优先按后缀判定：.engine 走 TensorRT，其它（.ctx.bin/.bin/.dlc/.onnx/.tflite）走 AidLite
        backend = endsWith(model_path, ".engine") ? InferBackend::TENSORRT : InferBackend::AIDLITE;
    }

    std::unique_ptr<detail::InferBackend> impl;
    switch (backend) {
        case InferBackend::TENSORRT: impl = detail::createTensorrtBackend(); break;
        case InferBackend::AIDLITE:  impl = detail::createAidliteBackend();  break;
        default:                     impl = detail::createNullBackend();     break;
    }

    if (!impl) {
        impl = detail::createNullBackend();
    }

    if (!impl->load(model_path, options)) {
        impl_->err = impl->error();
        if (impl_->err.empty()) impl_->err = "infer: backend load failed";
        rm::message(impl_->err, rm::MSG_ERROR);
        return false;
    }

    impl_->backend = std::move(impl);

    // 后端探测到的模型真实 IO 优先于配置文件：
    // AIMO/QNN 常常把 NCHW 转成 NHWC，这一步能自动纠正，避免"不报错但检测全乱"
    const auto& info = impl_->backend->info();
    if (info.valid) {
        if (info.input_width > 0 && info.input_height > 0) {
            if (info.input_width != impl_->opt.input_width ||
                info.input_height != impl_->opt.input_height) {
                rm::message(
                    "Infer: model input is " + std::to_string(info.input_width) + "x" +
                    std::to_string(info.input_height) + ", overriding config " +
                    std::to_string(impl_->opt.input_width) + "x" +
                    std::to_string(impl_->opt.input_height),
                    rm::MSG_WARNING);
                impl_->opt.input_width = info.input_width;
                impl_->opt.input_height = info.input_height;
            }
        }
        if (info.input_layout != impl_->opt.input_layout) {
            rm::message(
                std::string("Infer: model input layout is ") +
                (info.input_layout == InferLayout::NHWC ? "NHWC" : "NCHW") +
                ", overriding config (NCHW/NHWC mismatch would corrupt detections)",
                rm::MSG_WARNING);
            impl_->opt.input_layout = info.input_layout;
        }
        if (info.input_type != impl_->opt.input_type) {
            impl_->opt.input_type = info.input_type;
        }
        if (info.output_bboxes_num > 0 && info.output_struct_num > 0) {
            if (info.output_bboxes_num != impl_->opt.output_bboxes_num ||
                info.output_struct_num != impl_->opt.output_struct_num) {
                rm::message(
                    "Infer: model output is " + std::to_string(info.output_bboxes_num) + "x" +
                    std::to_string(info.output_struct_num) + ", overriding config " +
                    std::to_string(impl_->opt.output_bboxes_num) + "x" +
                    std::to_string(impl_->opt.output_struct_num),
                    rm::MSG_WARNING);
                impl_->opt.output_bboxes_num = info.output_bboxes_num;
                impl_->opt.output_struct_num = info.output_struct_num;
            }
        }
    }

    impl_->is_loaded = true;
    impl_->start();

    rm::message(
        std::string("Infer ready: ") + rm::inferBackendName(backend) + " <- " + model_path,
        rm::MSG_OK);
    return true;
}

bool Infer::setInput(const cv::Mat& bgr) {
    if (!impl_->is_loaded || !impl_->backend) return false;

    // 输入张量只有一份：如果上一次推理还没被 synchronize() 取走，
    // 这里必须等它跑完，否则会边推理边覆盖输入。
    {
        std::unique_lock<std::mutex> lock(impl_->mtx);
        impl_->cv_done.wait(lock, [this] { return impl_->job_done; });
    }

    void* dst = impl_->backend->inputBuffer();
    if (dst == nullptr) {
        impl_->err = "infer: backend input buffer is null";
        return false;
    }

    const bool ok = writeTensor(
        bgr,
        dst,
        impl_->opt.input_width,
        impl_->opt.input_height,
        impl_->opt.input_layout,
        impl_->opt.input_type,
        impl_->opt.pad_value,
        impl_->opt.keep_aspect_ratio);

    if (!ok) impl_->err = "infer: setInput failed (image must be CV_8UC3)";
    return ok;
}

void Infer::enqueue() {
    if (!impl_->is_loaded) return;
    std::unique_lock<std::mutex> lock(impl_->mtx);
    impl_->has_job = true;
    impl_->job_done = false;
    lock.unlock();
    impl_->cv_job.notify_one();
}

const float* Infer::synchronize() {
    if (!impl_->is_loaded || !impl_->backend) return nullptr;

    std::unique_lock<std::mutex> lock(impl_->mtx);
    impl_->cv_done.wait(lock, [this] { return impl_->job_done; });
    const bool failed = impl_->job_failed;
    lock.unlock();

    if (failed) {
        impl_->err = impl_->backend->error();
        if (impl_->err.empty()) impl_->err = "infer: invoke failed";
        return nullptr;
    }
    return impl_->backend->outputBuffer();
}

const float* Infer::detect(const cv::Mat& bgr) {
    if (!setInput(bgr)) return nullptr;
    enqueue();
    return synchronize();
}

bool Infer::loaded() const { return impl_->is_loaded; }
int  Infer::outputBboxesNum() const { return impl_->opt.output_bboxes_num; }
int  Infer::outputStructNum() const { return impl_->opt.output_struct_num; }
const std::string& Infer::lastError() const { return impl_->err; }

// 供配置文件使用的小工具
namespace rm {
InferBackend inferBackendFromString(const std::string& s) { return backendFromString(s); }
InferLayout  inferLayoutFromString(const std::string& s) { return layoutFromString(s); }
InferDType   inferDTypeFromString(const std::string& s) { return dtypeFromString(s); }
}
