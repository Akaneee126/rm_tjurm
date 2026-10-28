// 鍒ゅ畾 AidLite uint8 妯″瀷杈撳叆鐨勯噺鍖栬涔夛細0-1 float / 0-255 float / uint8 native
#include <aidlux/aidlite/aidlite.hpp>
#include <opencv2/opencv.hpp>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include <unistd.h>

namespace aal = Aidlux::Aidlite;
using namespace aal;

static const int IW = 640, IH = 640, NB = 25200, NS = 22;

static void dumpIn(const char* tag, const TensorInfo& t) {
    printf("%s name=%s type=%d elems=%llu shape=[", tag, t.name.c_str(),
           (int)t.element_type, (unsigned long long)t.element_count);
    for (size_t k = 0; k < t.shape.size(); ++k)
        printf("%s%llu", k ? "," : "", (unsigned long long)t.shape[k]);
    printf("]\n");
}

int main(int argc, char** argv) {
    const char* model = argv[1];

    cv::Mat bgr = cv::imread("/root/smoke/armor1.png");
    if (bgr.empty()) { printf("no image\n"); return 1; }

    Model* m = Model::create_instance(model);
    Config* c = Config::create_instance();
    c->framework_type = FrameworkType::TYPE_QNN;
    c->accelerate_type = AccelerateType::TYPE_DSP;
    c->implement_type = ImplementType::TYPE_LOCAL;
    auto it = InterpreterBuilder::build_interpreter_from_model_and_config(m, c);
    if (!it || it->init() != 0 || it->load_model() != 0) { printf("FAIL load\n"); return 1; }

    std::vector<std::vector<TensorInfo>> in_info, out_info;
    it->get_input_tensor_info(in_info);
    it->get_output_tensor_info(out_info);
    if (!in_info.empty() && !in_info[0].empty()) dumpIn("IN ", in_info[0][0]);
    if (!out_info.empty() && !out_info[0].empty()) dumpIn("OUT", out_info[0][0]);

    // 涓?C++ 绔竴鑷寸殑 letterbox锛氱瓑姣?灞呬腑+114锛孯GB锛孨HWC
    const int sw_src = bgr.cols, sh_src = bgr.rows;
    const float sc = std::min((float)IW / sw_src, (float)IH / sh_src);
    int sw = std::max(1, (int)lround(sw_src * sc)), sh = std::max(1, (int)lround(sh_src * sc));
    cv::Mat canvas(IH, IW, CV_8UC3, cv::Scalar::all(114));
    cv::Mat rs; cv::resize(bgr, rs, cv::Size(sw, sh));
    rs.copyTo(canvas(cv::Rect((IW - sw) / 2, (IH - sh) / 2, sw, sh)));
    cv::Mat rgb; cv::cvtColor(canvas, rgb, cv::COLOR_BGR2RGB);

    const size_t n = (size_t)IW * IH * 3;
    std::vector<unsigned char> u8(n);
    std::memcpy(u8.data(), rgb.data, n);
    std::vector<float> f01(n), f255(n);
    for (size_t i = 0; i < n; ++i) { f01[i] = u8[i] / 255.0f; f255[i] = (float)u8[i]; }

    std::vector<float> A, B, D;
    void* out = nullptr; uint32_t out_len = 0;

    auto run = [&](void* t, bool native, std::vector<float>& dst) {
        for (int i = 0; i < 3; ++i) { it->set_input_tensor(0, t, native); it->invoke(); }
        it->set_input_tensor(0, t, native);
        it->invoke();
        out = nullptr; out_len = 0;
        it->get_output_tensor(0, &out, &out_len);
        dst.assign((float*)out, (float*)out + out_len / 4);
    };

    run(u8.data(), true, A);
    run(f01.data(), false, B);
    run(f255.data(), false, D);

    auto cmp = [&](const std::vector<float>& x, const std::vector<float>& y, const char* tag) {
        double s = 0, mx = 0; size_t cnt = 0;
        for (size_t i = 0; i < x.size() && i < y.size(); ++i) {
            double d = std::fabs((double)x[i] - (double)y[i]);
            s += d; if (d > mx) mx = d; ++cnt;
        }
        printf("%s : mean|d|=%.6f  max|d|=%.3f\n", tag, s / (double)cnt, mx);
    };

    auto confmax = [&](const std::vector<float>& v, const char* tag) {
        float best = -1e9f; int bi = -1;
        for (int i = 0; i < NB; ++i) { float q = v[i * NS + 8]; if (q > best) { best = q; bi = i; } }
        printf("%s max conf=%.4f rec=%d\n", tag, best, bi);
        if (bi >= 0 && best > 0.05f) {
            const float* r = v.data() + bi * NS;
            printf("   pts:");
            for (int k = 0; k < 8; k += 2) printf(" (%.1f,%.1f)", r[k], r[k + 1]);
            printf("\n   col:");
            for (int k = 0; k < 4; ++k) printf(" %.2f", r[9 + k]);
            printf("\n   cls:");
            for (int k = 0; k < 9; ++k) printf(" %.2f", r[13 + k]);
            printf("\n");
        }
    };

    confmax(A, "A u8/native  ");
    confmax(B, "B f01/quant  ");
    confmax(D, "D f255/quant ");
    cmp(A, B, "A vs B");
    cmp(A, D, "A vs D");
    cmp(B, D, "B vs D");
    fflush(stdout);
    _exit(0);
}
