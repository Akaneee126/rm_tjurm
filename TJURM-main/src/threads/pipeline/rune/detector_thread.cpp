#include "threads/pipeline.h"
#include "data_manager/model.h"
#include <unistd.h>
#include <iostream>
#include <thread>

using namespace rm;

void Pipeline::detector_rune_thread(
    std::mutex& mutex_in, bool& flag_in, std::shared_ptr<rm::Frame>& frame_in, 
    std::mutex& mutex_out, bool& flag_out, std::shared_ptr<rm::Frame>& frame_out
) {
    auto param = Param::get_instance();

    nlohmann::json& model_cfg = (*param)["Model"]["YoloRune"];

    const int    infer_width       = ModelConfig::intOf(model_cfg, "InferWidth", 640);
    const int    infer_height      = ModelConfig::intOf(model_cfg, "InferHeight", 512);
    const int    class_num         = ModelConfig::intOf(model_cfg, "ClassNum", 2);
    const int    bboxes_num        = ModelConfig::intOf(model_cfg, "BboxesNum", 20160);
    const double confidence_thresh = ModelConfig::numOf(model_cfg, "ConfThresh", 0.95);
    const double nms_thresh        = ModelConfig::numOf(model_cfg, "NMSThresh", 0.30);

    std::mutex mutex;
    TimePoint tp0, tp1, tp2;
    rm::CycleQueue<double> delay_list(100);

    while (true) {
        if (!Data::rune_mode) {
            std::unique_lock<std::mutex> lock(mutex);
            rune_cv_.wait(lock, [this]{return Data::rune_mode;});
        }

        tp0 = getTime();
        while(!flag_in) {
            if (getDoubleOfS(tp0, getTime()) > 10.0 && Data::timeout_flag) {
                rm::message("Detector timeout", rm::MSG_ERROR);
                exit(-1);
            }
            std::this_thread::yield();
        }

        std::shared_ptr<rm::Frame> frame;
        {
            std::unique_lock<std::mutex> lock_in(mutex_in);
            frame = frame_in;
        }

        // 本帧在工作期间保持 flag_in=true（生产者会等它变 false），
        // 保证 NMS 读完输出缓冲之前生产者不会投递下一帧。
        struct FlagGuard {
            std::mutex& mutex;
            bool&       flag;
            ~FlagGuard() { std::lock_guard<std::mutex> lock(mutex); flag = false; }
        } flag_guard{mutex_in, flag_in};

        tp1 = getTime();

        auto infer = std::atomic_load(&rune_infer_);
        if (!infer) {
            rm::message("Detector: rune inference engine is not ready", rm::MSG_ERROR);
            continue;
        }

        const float* output = infer->synchronize();
        if (output == nullptr) {
            rm::message("Inference failed: " + infer->lastError(), rm::MSG_ERROR);
            continue;
        }

        frame->yolo_list = yoloArmorNMS_FP(
            const_cast<float*>(output),
            bboxes_num,
            class_num,
            confidence_thresh,
            nms_thresh,
            frame->width,
            frame->height,
            infer_width,
            infer_height
        );

        if (frame->yolo_list.empty()) {
            if (Data::image_flag) imshow(frame);
            continue;
        }

        tp2 = getTime();
        if (Data::pipeline_delay_flag) rm::message("detect time", getDoubleOfS(tp1, tp2) * 1000);

        std::unique_lock<std::mutex> lock_out(mutex_out);
        frame_out = frame;
        flag_out = true;
        lock_out.unlock();
        rune_in_cv_.notify_one();
    }
}
