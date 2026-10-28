#include "threads/pipeline.h"
#include "data_manager/model.h"
#include <unistd.h>
#include <iostream>
#include <thread>

using namespace rm;

void Pipeline::detector_baseline_thread(
    std::mutex& mutex_in, bool& flag_in, std::shared_ptr<rm::Frame>& frame_in, 
    std::mutex& mutex_out, bool& flag_out, std::shared_ptr<rm::Frame>& frame_out
) {
    auto param = Param::get_instance();

    const std::string yolo_type = (*param)["Model"]["YoloArmor"]["Type"];
    nlohmann::json& model_cfg = (*param)["Model"]["YoloArmor"][yolo_type];

    // 部分导出把 sigmoid 留在图外（输出 logits），需要在这里打开
    const bool apply_sigmoid = ModelConfig::boolOf(model_cfg, "ApplySigmoid", false);
    rm::yoloSetSigmoid(apply_sigmoid);
    if (apply_sigmoid) rm::message("YOLO output treated as logits (sigmoid applied)", rm::MSG_NOTE);

    // 四点顺序模式（0=旧模型默认，3=模型直接输出 左上-左下-右下-右上）
    rm::yoloSetPointOrder(ModelConfig::intOf(model_cfg, "PointOrder", 0));

    const int    infer_width       = ModelConfig::intOf(model_cfg, "InferWidth", 640);
    const int    infer_height      = ModelConfig::intOf(model_cfg, "InferHeight", 640);
    const int    class_num         = ModelConfig::intOf(model_cfg, "ClassNum", 9);
    const int    color_num         = ModelConfig::intOf(model_cfg, "ColorNum", 4);
    const int    bboxes_num        = ModelConfig::intOf(model_cfg, "BboxesNum", 25200);
    const double confidence_thresh = ModelConfig::numOf(model_cfg, "ConfThresh", 0.3);
    const double nms_thresh        = ModelConfig::numOf(model_cfg, "NMSThresh", 0.02);

    std::mutex mutex;
    TimePoint tp0, tp1, tp2;
    while (true) {
        if (!Data::armor_mode) {
            std::unique_lock<std::mutex> lock(mutex);
            armor_cv_.wait(lock, [this]{return Data::armor_mode;});
        }

        tp0 = getTime();
        while(!flag_in) {
            if (getDoubleOfS(tp0, getTime()) > 10.0 && Data::timeout_flag) {
                rm::message("Detector timeout", rm::MSG_ERROR);
                exit(-1);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(0));
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

        auto infer = std::atomic_load(&armor_infer_);
        if (!infer) {
            rm::message("Detector: inference engine is not ready", rm::MSG_ERROR);
            continue;
        }

        // 阻塞等待本次推理完成，拿到 host 侧输出：[bboxes][locate+conf+color+class]
        const float* output = infer->synchronize();
        if (output == nullptr) {
            rm::message("Inference failed: " + infer->lastError(), rm::MSG_ERROR);
            continue;
        }

        float* output_host_buffer = const_cast<float*>(output);

        if (yolo_type == "V5") {
            frame->yolo_list = yoloArmorNMS_V5(
                output_host_buffer,
                bboxes_num,
                class_num,
                confidence_thresh,
                nms_thresh,
                frame->width,
                frame->height,
                infer_width,
                infer_height
            );
        } else if (yolo_type == "FP") {
            frame->yolo_list = yoloArmorNMS_FP(
                output_host_buffer,
                bboxes_num,
                class_num,
                confidence_thresh,
                nms_thresh,
                frame->width,
                frame->height,
                infer_width,
                infer_height
            );
        } else if (yolo_type == "FPX") {
            frame->yolo_list = yoloArmorNMS_FPX(
                output_host_buffer,
                bboxes_num,
                class_num,
                confidence_thresh,
                nms_thresh,
                frame->width,
                frame->height,
                infer_width,
                infer_height,
                color_num
            );
        } else {
            rm::message("Invalid yolo type", rm::MSG_ERROR);
            exit(-1);
        }

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
        tracker_in_cv_.notify_one();
    }
}
