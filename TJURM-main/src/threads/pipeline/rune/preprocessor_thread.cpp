#include "threads/pipeline.h"
#include "data_manager/model.h"
#include <unistd.h>
#include <iostream>
#include <thread>

using namespace rm;

void Pipeline::preprocessor_rune_thread(
    std::mutex& mutex_out, bool& flag_out, std::shared_ptr<rm::Frame>& frame_out
) {
    auto param = Param::get_instance();

    nlohmann::json& model_cfg = (*param)["Model"]["YoloRune"];

    const std::string model_path = ModelConfig::pathOf(model_cfg);
    const rm::InferOptions infer_opt = ModelConfig::optionsOf(model_cfg);

    if (model_path.empty()) {
        rm::message("Rune model path is empty, check Config.json Model.YoloRune", rm::MSG_ERROR);
        exit(-1);
    }

    // 先构建并加载好，再发布给检测器线程（atomic_store 保证跨线程可见性）
    auto infer = std::make_shared<rm::Infer>();
    if (!infer->load(model_path, infer_opt)) {
        rm::message("Rune inference engine load failed", rm::MSG_ERROR);
        exit(-1);
    }
    std::atomic_store(&rune_infer_, infer);

    std::mutex mutex;
    TimePoint frame_wait, flag_wait;
    TimePoint tp0, tp1, tp2;
    while(true) {
        if (!Data::rune_mode) {
            std::unique_lock<std::mutex> lock(mutex);
            rune_cv_.wait(lock, [this]{return Data::rune_mode;});
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

        if (!infer->setInput(*frame->image)) {
            rm::message("Preprocess failed: " + infer->lastError(), rm::MSG_ERROR);
            continue;
        }
        infer->enqueue();

        if (Data::record_mode) { record(frame); }

        tp2 = getTime();
        if (Data::pipeline_delay_flag) rm::message("preprocess", getDoubleOfS(tp1, tp2) * 1000);

        std::unique_lock<std::mutex> lock_out(mutex_out);
        frame_out = frame;
        flag_out = true;
    }
}
