// Decode test: sigmoid on ch8..21, draw best box per input mode
#include <aidlux/aidlite/aidlite.hpp>
#include <opencv2/opencv.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include <unistd.h>

namespace aal = Aidlux::Aidlite;
using namespace aal;
static const int IW = 640, IH = 640, NB = 25200, NS = 22;

static inline float sig(float x) { return 1.0f / (1.0f + std::exp(-x)); }

static void letterbox(const cv::Mat& bgr, std::vector<unsigned char>& u8, std::vector<float>& f01,
                      float& sc_out, int& px_out, int& py_out) {
    u8.assign((size_t)IW * IH * 3, 114);
    const float sc = std::min((float)IW / bgr.cols, (float)IH / bgr.rows);
    int sw = std::max(1, (int)lround(bgr.cols * sc)), sh = std::max(1, (int)lround(bgr.rows * sc));
    cv::Mat rs; cv::resize(bgr, rs, cv::Size(sw, sh));
    cv::Mat canvas(IH, IW, CV_8UC3, cv::Scalar::all(114));
    rs.copyTo(canvas(cv::Rect((IW - sw) / 2, (IH - sh) / 2, sw, sh)));
    cv::Mat rgb; cv::cvtColor(canvas, rgb, cv::COLOR_BGR2RGB);
    std::memcpy(u8.data(), rgb.data, (size_t)IW * IH * 3);
    f01.resize(u8.size());
    for (size_t i = 0; i < u8.size(); ++i) f01[i] = u8[i] / 255.0f;
    sc_out = sc; px_out = (IW - sw) / 2; py_out = (IH - sh) / 2;
}

int main(int argc, char** argv) {
    const char* model = argv[1];
    const char* img_path = (argc > 2) ? argv[2] : "/root/smoke/f120.jpg";

    cv::Mat bgr = cv::imread(img_path);
    if (bgr.empty()) { printf("no img\n"); return 1; }
    printf("image %dx%d\n", bgr.cols, bgr.rows);

    std::vector<unsigned char> u8;
    std::vector<float> f01;
    float sc; int px, py;
    letterbox(bgr, u8, f01, sc, px, py);

    Model* m = Model::create_instance(model);
    Config* c = Config::create_instance();
    c->framework_type = FrameworkType::TYPE_QNN;
    c->accelerate_type = AccelerateType::TYPE_DSP;
    c->implement_type = ImplementType::TYPE_LOCAL;
    auto it = InterpreterBuilder::build_interpreter_from_model_and_config(m, c);
    if (!it || it->init() != 0 || it->load_model() != 0) { printf("FAIL\n"); return 1; }

    void* out = nullptr; uint32_t len = 0;

    auto run = [&](void* p, bool native, const char* tag, const char* save) {
        it->set_input_tensor(0, p, native);
        it->invoke();
        out = nullptr; len = 0;
        it->get_output_tensor(0, &out, &len);
        const float* v = (const float*)out;

        int bi = -1; float bconf = -1;
        for (int i = 0; i < NB; ++i) {
            float cf = sig(v[i * NS + 8]);
            if (cf > bconf) { bconf = cf; bi = i; }
        }
        printf("\n[%s] best obj conf=%.4f @ rec %d\n", tag, bconf, bi);
        if (bi < 0) return;
        const float* r = v + bi * NS;
        printf("   pts(lb):");
        for (int k = 0; k < 8; k += 2) printf(" (%.1f,%.1f)", r[k], r[k + 1]);
        printf("\n   color sig:");
        for (int k = 0; k < 4; ++k) printf(" %.3f", sig(r[9 + k]));
        printf("\n   class sig:");
        for (int k = 0; k < 9; ++k) printf(" %.3f", sig(r[13 + k]));
        printf("\n");

        cv::Mat vis = bgr.clone();
        std::vector<cv::Point> poly;
        for (int k = 0; k < 4; ++k) {
            float X = (r[k * 2] - px) / sc;
            float Y = (r[k * 2 + 1] - py) / sc;
            poly.push_back(cv::Point((int)X, (int)Y));
        }
        cv::polylines(vis, poly, true, cv::Scalar(0, 255, 0), 4);
        char txt[64];
        snprintf(txt, sizeof(txt), "%s conf=%.3f", tag, bconf);
        cv::putText(vis, txt, cv::Point(20, 40), cv::FONT_HERSHEY_SIMPLEX, 1.0,
                    cv::Scalar(0, 255, 255), 2);
        cv::imwrite(save, vis);
        printf("   saved %s\n", save);
    };

    run(u8.data(), true, "A_u8native", "/root/smoke/dec_A.jpg");
    run(f01.data(), false, "B_f01", "/root/smoke/dec_B.jpg");
    std::vector<float> f255(u8.size());
    for (size_t i = 0; i < u8.size(); ++i) f255[i] = (float)u8[i];
    run(f255.data(), false, "D_f255", "/root/smoke/dec_D.jpg");
    fflush(stdout);
    _exit(0);
}