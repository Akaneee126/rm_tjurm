#ifndef RM2024_DATA_MANAGER_MODEL_H_
#define RM2024_DATA_MANAGER_MODEL_H_

// 把 Config.json 中 "Model" 节点翻译成 rm::InferOptions。
// 这样上层线程不需要知道后端是 TensorRT 还是 AidLite(QNN)。

#include <string>

#include <unistd.h>

#include <json.hpp>

#include <infer/infer.h>

namespace ModelConfig {

inline double numOf(const nlohmann::json& j, const char* key, double def) {
    if (j.contains(key) && j[key].is_number()) return j[key].get<double>();
    return def;
}

inline int intOf(const nlohmann::json& j, const char* key, int def) {
    return static_cast<int>(numOf(j, key, static_cast<double>(def)));
}

inline bool boolOf(const nlohmann::json& j, const char* key, bool def) {
    if (j.contains(key) && j[key].is_boolean()) return j[key].get<bool>();
    return def;
}

inline std::string strOf(const nlohmann::json& j, const char* key, const std::string& def) {
    if (j.contains(key) && j[key].is_string()) return j[key].get<std::string>();
    return def;
}

// 模型路径：优先 DirModel；否则回退旧的 DirEngine / DirONNX
inline std::string pathOf(const nlohmann::json& j) {
    const std::string dir_model = strOf(j, "DirModel", "");
    if (!dir_model.empty()) return dir_model;

    const std::string dir_engine = strOf(j, "DirEngine", "");
    const std::string dir_onnx   = strOf(j, "DirONNX", "");

    if (!dir_engine.empty() && access(dir_engine.c_str(), F_OK) == 0) return dir_engine;
    if (!dir_onnx.empty()) return dir_onnx;
    return dir_engine;
}

inline rm::InferOptions optionsOf(const nlohmann::json& j) {
    rm::InferOptions opt;

    opt.input_width  = intOf(j, "InferWidth", 640);
    opt.input_height = intOf(j, "InferHeight", 640);

    const int locate_num = intOf(j, "LocateNum", 8);
    const int color_num  = intOf(j, "ColorNum", 4);
    const int class_num  = intOf(j, "ClassNum", 9);

    opt.output_bboxes_num = intOf(j, "BboxesNum", 25200);
    opt.output_struct_num = intOf(j, "StructNum", locate_num + 1 + color_num + class_num);

    opt.input_layout = rm::inferLayoutFromString(strOf(j, "InputLayout", "NCHW"));
    opt.input_type   = rm::inferDTypeFromString(strOf(j, "InputType", "FLOAT32"));
    opt.pad_value    = static_cast<float>(numOf(j, "PadValue", 114.0));
    opt.keep_aspect_ratio = boolOf(j, "KeepAspectRatio", true);

    opt.backend   = rm::inferBackendFromString(strOf(j, "Backend", "AUTO"));
    opt.framework = strOf(j, "Framework", "QNN");
    opt.accelerate = strOf(j, "Accelerate", "DSP");
    opt.qnn_version = strOf(j, "QNNVersion", "");
    opt.backend_extension_config = strOf(j, "HtpConfig", "");
    opt.threads = intOf(j, "Threads", 2);

    return opt;
}

}

#endif
