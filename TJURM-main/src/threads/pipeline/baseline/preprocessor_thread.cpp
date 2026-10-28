#include "threads/pipeline.h"
#include "data_manager/model.h"
#include <unistd.h>
#include <iostream>
#include <thread>

using namespace rm;

void Pipeline::preprocessor_baseline_thread(
    std::mutex& mutex_out, bool& flag_out, std::shared_ptr<rm::Frame>& frame_out
) {
    auto param = Param::get_instance();

    const std::string yolo_type = (*param)["Model"]["YoloArmor"]["Type"];
    nlohmann::json& model_cfg = (*param)["Model"]["YoloArmor"][yolo_type];

    const std::string model_path = ModelConfig::pathOf(model_cfg);
    const rm::InferOptions infer_opt = ModelConfig::optionsOf(model_cfg);

    if (model_path.empty()) {
        rm::message("Armor model path is empty, check Config.json Model.YoloArmor." + yolo_type, rm::MSG_ERROR);
        exit(-1);
    }

    // 先构建并加载好，再发布给检测器线程（atomic_store 保证跨线程可见性）
    auto infer = std::make_shared<rm::Infer>();
    if (!infer->load(model_path, infer_opt)) {
        rm::message("Armor inference engine load failed", rm::MSG_ERROR);
        exit(-1);
    }
    std::atomic_store(&armor_infer_, infer);

    std::mutex mutex;
    TimePoint frame_wait, flag_wait;
    TimePoint tp0, tp1, tp2;
    while(true) {
        if (!Data::armor_mode) {
            std::unique_lock<std::mutex> lock(mutex);
            armor_cv_.wait(lock, [this]{return Data::armor_mode;});
        }

        // 等上一帧被检测器完全消费（含 NMS）后再准备下一帧，
        // 否则会覆盖推理的输入/输出缓冲
        flag_wait = getTime();
        while(flag_out) {
            if (getDoubleOfS(flag_wait, getTime()) > 10.0 && Data::timeout_flag) {
                rm::message("Preprocessor timeout", rm::MSG_ERROR);
                exit(-1);
            }
            std::this_thread::yield();
        }

        Camera* camera = Data::camera[Data::camera_index];
        std::shared_ptr<rm::Frame> frame = camera->buffer->pop();

        frame_wait = tp1 = getTime();
        while(frame == nullptr) {
            frame = camera->buffer->pop();
            double delay = getDoubleOfS(frame_wait, getTime());
            if (delay > 0.5 && Data::timeout_flag) {
                rm::message("Capture timeout", rm::MSG_ERROR);
                exit(-1);
            }
        }

        // letterbox + RGB + 归一化 + NCHW/NHWC，纯 CPU(OpenCV)，不再需要 CUDA
        if (!infer->setInput(*frame->image)) {
            rm::message("Preprocess failed: " + infer->lastError(), rm::MSG_ERROR);
            continue;
        }

        // 投递到推理后端（AidLite 为同步 invoke，内部工作线程执行）
        infer->enqueue();

        if (Data::record_mode) { record(frame); }

        tp2 = getTime();
        if (Data::pipeline_delay_flag) rm::message("preprocess", getDoubleOfS(tp1, tp2) * 1000);

        std::unique_lock<std::mutex> lock_out(mutex_out);
        frame_out = frame;
        flag_out = true;
    }
}
