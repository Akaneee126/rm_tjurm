// Standalone AidLite smoke test (no OpenCV / Eigen needed).
// 楠岃瘉锛?aidem 鑳藉惁鍔犺浇銆丵NN 鐗堟湰鏄惁鍖归厤銆佺湡瀹?IO 甯冨眬銆佸崟甯у欢杩熴€?// 缂栬瘧锛?g++ -O2 -std=c++17 aidlite_smoke.cpp -I/usr/local/include -L/usr/local/lib -laidlite -o aidlite_smoke
// 杩愯锛?LD_LIBRARY_PATH=/usr/local/lib ./aidlite_smoke <model> [runs]

#include <aidlux/aidlite/aidlite.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace aal = Aidlux::Aidlite;
using namespace aal;

static const char* dtypeName(DataType t) {
    switch (static_cast<int>(t)) {
        case 1: return "UINT8";
        case 2: return "INT8";
        case 4: return "FLOAT32";
        case 8: return "INT16";
        case 10: return "FLOAT16";
        default: return "OTHER";
    }
}

static void dumpInfo(const char* tag, std::vector<std::vector<TensorInfo>>& info) {
    for (size_t i = 0; i < info.size(); ++i) {
        for (size_t j = 0; j < info[i].size(); ++j) {
            const TensorInfo& t = info[i][j];
            printf("%s[%zu][%zu] name=%s type=%s elems=%llu dims=%u shape=[",
                   tag, i, j, t.name.c_str(), dtypeName(t.element_type),
                   (unsigned long long)t.element_count, t.dimensions);
            for (size_t k = 0; k < t.shape.size(); ++k) {
                printf("%s%llu", k ? "," : "", (unsigned long long)t.shape[k]);
            }
            printf("]\n");
        }
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("usage: %s <model> [runs]\n", argv[0]);
        return 2;
    }
    const std::string model_path = argv[1];
    const int runs = (argc > 2) ? atoi(argv[2]) : 5;

    printf("aidlite library : %s\n", aal::get_library_version().c_str());
    printf("model           : %s\n", model_path.c_str());

    Model* model = Model::create_instance(model_path);
    if (!model) {
        printf("FAIL: create_instance\n");
        return 1;
    }

    Config* config = Config::create_instance();
    if (!config) {
        printf("FAIL: config\n");
        return 1;
    }
    config->framework_type = FrameworkType::TYPE_QNN;      // 鑷姩閫夊凡瀹夎鐨勬渶楂?QNN 鍚庣
    config->accelerate_type = AccelerateType::TYPE_DSP;    // 楂橀€?DSP = HTP/NPU
    config->implement_type = ImplementType::TYPE_LOCAL;
    config->is_quantify_model = 0;

    auto interp = InterpreterBuilder::build_interpreter_from_model_and_config(model, config);
    if (!interp) {
        printf("FAIL: build_interpreter\n");
        return 1;
    }
    int rc = interp->init();
    printf("init     rc=%d\n", rc);
    if (rc != 0) return 1;
    rc = interp->load_model();
    printf("load     rc=%d\n", rc);
    if (rc != 0) return 1;

    std::vector<std::vector<TensorInfo>> in_info, out_info;
    rc = interp->get_input_tensor_info(in_info);
    printf("in_info  rc=%d\n", rc);
    if (rc == 0) dumpInfo("  in", in_info);
    rc = interp->get_output_tensor_info(out_info);
    printf("out_info rc=%d\n", rc);
    if (rc == 0) dumpInfo("  out", out_info);

    if (in_info.empty() || in_info[0].empty()) {
        printf("FAIL: no input info\n");
        return 1;
    }
    const TensorInfo& in = in_info[0][0];
    const size_t elems = static_cast<size_t>(in.element_count);
    const bool is_float = (static_cast<int>(in.element_type) == 4);

    // 鐢?0.447 (=114/255锛宭etterbox 濉厖鑹? 濉厖杈撳叆锛屽彧娴嬮€氶摼璺笌鑰楁椂
    std::vector<float> input_f(elems, 0.447f);
    std::vector<uint8_t> input_u8(elems, 114);
    void* input_ptr = is_float ? (void*)input_f.data() : (void*)input_u8.data();

    for (int i = 0; i < 2; ++i) {   // warmup
        interp->set_input_tensor(0, input_ptr);
        interp->invoke();
    }

    double total = 0, mn = 1e9, mx = 0;
    void* out = nullptr;
    uint32_t out_len = 0;
    float omni = 1e9f, omax = -1e9f;
    for (int i = 0; i < runs; ++i) {
        auto t0 = std::chrono::steady_clock::now();
        rc = interp->set_input_tensor(0, input_ptr);
        if (rc != 0) { printf("FAIL set_input rc=%d\n", rc); return 1; }
        rc = interp->invoke();
        if (rc != 0) { printf("FAIL invoke rc=%d\n", rc); return 1; }
        out = nullptr; out_len = 0;
        rc = interp->get_output_tensor(0, &out, &out_len);
        if (rc != 0 || !out) { printf("FAIL get_output rc=%d\n", rc); return 1; }
        auto t1 = std::chrono::steady_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        total += ms;
        if (ms < mn) mn = ms;
        if (ms > mx) mx = ms;

        const float* f = static_cast<const float*>(out);
        const size_t n = out_len / sizeof(float);
        for (size_t k = 0; k < n; ++k) {
            if (f[k] < omni) omni = f[k];
            if (f[k] > omax) omax = f[k];
        }
        if (i == 0) printf("out bytes=%u floats=%zu\n", out_len, n);
    }

    printf("infer  avg=%.2f ms  min=%.2f  max=%.2f  (%d runs)\n", total / runs, mn, mx, runs);
    printf("out range [%.4f, %.4f]\n", omni, omax);
    printf("OK\n");
    return 0;
}
