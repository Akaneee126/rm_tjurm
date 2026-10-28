#include "threads/pipeline.h"
#include <thread>

void Pipeline::autoaim_fourpoints() {
    // 推理后端（TensorRT / AidLite-QNN）在各自的预处理器线程中初始化
    Data::armor_mode = true;
    Data::rune_mode = false;
    Data::defence_mode = false;
    Data::record_mode = false;

    preprocessor_over_ = false;
    detector_over_ = false;
    
    std::thread preprocessor_thread(
        &Pipeline::preprocessor_fourpoints_thread, this,
        std::ref(preprocessor_mutex_), std::ref(preprocessor_over_), std::ref(preprocessor_register_));
    
    std::thread detector_thread(
        &Pipeline::detector_fourpoints_thread, this,
        std::ref(preprocessor_mutex_), std::ref(preprocessor_over_), std::ref(preprocessor_register_));

    std::thread recording_thread(
        &Pipeline::recording_thread, this,
        std::ref(record_mutex_), std::ref(record_in_), std::ref(record_register_));

    preprocessor_thread.detach();
    detector_thread.detach();
    recording_thread.detach();

    if (Data::image_flag) {
        std::thread image_thread(&Pipeline::image_thread, this);
        image_thread.detach();
    }
}

void Pipeline::autoaim_baseline() {
    // 推理后端（TensorRT / AidLite-QNN）在各自的预处理器线程中初始化
    Data::armor_mode = true;
    Data::rune_mode = false;
    Data::defence_mode = false;
    Data::record_mode = false;

    preprocessor_over_ = false;
    detector_over_ = false;
    
    std::thread preprocessor_baseline_thread(
        &Pipeline::preprocessor_baseline_thread, this,
        std::ref(preprocessor_mutex_), std::ref(preprocessor_over_), std::ref(preprocessor_register_));
    
    std::thread detector_baseline_thread(
        &Pipeline::detector_baseline_thread, this,
        std::ref(preprocessor_mutex_), std::ref(preprocessor_over_), std::ref(preprocessor_register_),
        std::ref(detector_mutex_), std::ref(detector_over_), std::ref(detector_register_));

    std::thread tracker_baseline_thread(
        &Pipeline::tracker_baseline_thread, this,
        std::ref(detector_mutex_), std::ref(detector_over_), std::ref(detector_register_));

    std::thread recording_thread(
        &Pipeline::recording_thread, this,
        std::ref(record_mutex_), std::ref(record_in_), std::ref(record_register_));

    preprocessor_baseline_thread.detach();
    detector_baseline_thread.detach();
    tracker_baseline_thread.detach();
    recording_thread.detach();

    if (Data::image_flag) {
        std::thread image_thread(&Pipeline::image_thread, this);
        image_thread.detach();
    }
}

void Pipeline::autoaim_rune() {
    // 推理后端（TensorRT / AidLite-QNN）在各自的预处理器线程中初始化
    Data::armor_mode = false;
    Data::rune_mode = true;
    Data::defence_mode = false;
    Data::record_mode = false;

    preprocessor_over_ = false;
    detector_over_ = false;   
    
    std::thread preprocessor_thread(
        &Pipeline::preprocessor_rune_thread, this,
        std::ref(preprocessor_mutex_), std::ref(preprocessor_over_), std::ref(preprocessor_register_));
    
    std::thread detector_thread(
        &Pipeline::detector_rune_thread, this,
        std::ref(preprocessor_mutex_), std::ref(preprocessor_over_), std::ref(preprocessor_register_),
        std::ref(detector_mutex_), std::ref(detector_over_), std::ref(detector_register_));

    std::thread tracker_thread(
        &Pipeline::tracker_rune_thread, this,
        std::ref(detector_mutex_), std::ref(detector_over_), std::ref(detector_register_));

    std::thread recording_thread(
        &Pipeline::recording_thread, this,
        std::ref(record_mutex_), std::ref(record_in_), std::ref(record_register_));

    preprocessor_thread.detach();
    detector_thread.detach();
    tracker_thread.detach();
    recording_thread.detach();

    if (Data::image_flag) {
        std::thread image_thread(&Pipeline::image_thread, this);
        image_thread.detach();
    }
}

void Pipeline::autoaim_combine() {
    // 推理后端（TensorRT / AidLite-QNN）在各自的预处理器线程中初始化
    Data::armor_mode = true;
    Data::rune_mode = false;
    Data::defence_mode = false;
    Data::record_mode = false;

    preprocessor_over_ = false;
    detector_over_ = false;
    
    std::thread preprocessor_baseline_thread(
        &Pipeline::preprocessor_baseline_thread, this,
        std::ref(preprocessor_mutex_), std::ref(preprocessor_over_), std::ref(preprocessor_register_));
    
    std::thread detector_baseline_thread(
        &Pipeline::detector_baseline_thread, this,
        std::ref(preprocessor_mutex_), std::ref(preprocessor_over_), std::ref(preprocessor_register_),
        std::ref(detector_mutex_), std::ref(detector_over_), std::ref(detector_register_));

    std::thread tracker_baseline_thread(
        &Pipeline::tracker_baseline_thread, this,
        std::ref(detector_mutex_), std::ref(detector_over_), std::ref(detector_register_));

    std::thread recording_thread(
        &Pipeline::recording_thread, this,
        std::ref(record_mutex_), std::ref(record_in_), std::ref(record_register_));

    preprocessor_baseline_thread.detach();
    detector_baseline_thread.detach();
    tracker_baseline_thread.detach();
    recording_thread.detach();

    #if defined(TJURM_INFANTRY) || defined(TJURM_BALANCE)
    if (Data::auto_rune || Data::manu_rune) {
        std::thread preprocessor_rune_thread(
        &Pipeline::preprocessor_rune_thread, this,
        std::ref(preprocessor_mutex_), std::ref(preprocessor_over_), std::ref(preprocessor_register_));
    
        std::thread detector_rune_thread(
            &Pipeline::detector_rune_thread, this,
            std::ref(preprocessor_mutex_), std::ref(preprocessor_over_), std::ref(preprocessor_register_),
            std::ref(detector_mutex_), std::ref(detector_over_), std::ref(detector_register_));

        std::thread tracker_rune_thread(
            &Pipeline::tracker_rune_thread, this,
            std::ref(detector_mutex_), std::ref(detector_over_), std::ref(detector_register_));

        preprocessor_rune_thread.detach();
        detector_rune_thread.detach();
        tracker_rune_thread.detach();
    }
    #endif

    if (Data::image_flag) {
        std::thread image_thread(&Pipeline::image_thread, this);
        image_thread.detach();
    }
}

void Pipeline::start_record() {
    Data::record_mode = true;
    record_cv_.notify_all();
}

void Pipeline::stop_record() {
    Data::record_mode = false;
}
    
void Pipeline::switch_armor_to_rune() {
    Data::armor_mode = false;
    Data::rune_mode = true;
    Data::defence_mode = false;
    preprocessor_over_ = false;
    detector_over_ = false;
    rune_cv_.notify_all();
}
    
void Pipeline::switch_rune_to_armor() {
    Data::armor_mode = true;
    Data::rune_mode = false;
    Data::defence_mode = false;
    preprocessor_over_ = false;
    detector_over_ = false;
    armor_cv_.notify_all();
}