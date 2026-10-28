// 用真实录制视频验证：三种输入编码下，谁真的能检出装甲板
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

static void letterbox(const cv::Mat& bgr, std::vector<unsigned char>& u8) {
    u8.assign((size_t)IW * IH * 3, 114);
    const float sc = std::min((float)IW / bgr.cols, (float)IH / bgr.rows);
    int sw = std::max(1, (int)lround(bgr.cols * sc)), sh = std::max(1, (int)lround(bgr.rows * sc));
    cv::Mat rs; cv::resize(bgr, rs, cv::Size(sw, sh));
    cv::Mat canvas(IH, IW, CV_8UC3, cv::Scalar::all(114));
    rs.copyTo(canvas(cv::Rect((IW - sw) / 2, (IH - sh) / 2, sw, sh)));
    cv::Mat rgb; cv::cvtColor(canvas, rgb, cv::COLOR_BGR2RGB);
    std::memcpy(u8.data(), rgb.data, (size_t)IW * IH * 3);
}

struct Det { float conf; int idx; };

static Det best(const float* v) {
    Det d{-1e9f, -1};
    for (int i = 0; i < NB; ++i) {
        float c = v[i * NS + 8];
        if (c > d.conf) { d.conf = c; d.idx = i; }
    }
    return d;
}

static int countAbove(const float* v, float th) {
    int n = 0;
    for (int i = 0; i < NB; ++i) if (v[i * NS + 8] > th) ++n;
    return n;
}

int main(int argc, char** argv) {
    const char* model = argv[1];
    const char* video = (argc > 2) ? argv[2] : "/root/smoke/scene.avi";

    cv::VideoCapture cap(video);
    if (!cap.isOpened()) { printf("FAIL open video %s\n", video); return 1; }
    const int total = (int)cap.get(cv::CAP_PROP_FRAME_COUNT);
    const int w = (int)cap.get(cv::CAP_PROP_FRAME_WIDTH);
    const int h = (int)cap.get(cv::CAP_PROP_FRAME_HEIGHT);
    const double fps = cap.get(cv::CAP_PROP_FPS);
    printf("video %dx%d fps=%.1f frames=%d\n", w, h, fps, total);

    Model* m = Model::create_instance(model);
    Config* c = Config::create_instance();
    c->framework_type = FrameworkType::TYPE_QNN;
    c->accelerate_type = AccelerateType::TYPE_DSP;
    c->implement_type = ImplementType::TYPE_LOCAL;
    auto it = InterpreterBuilder::build_interpreter_from_model_and_config(m, c);
    if (!it || it->init() != 0 || it->load_model() != 0) { printf("FAIL load\n"); return 1; }

    const int N = 8;
    std::vector<float> resA, resB, resD;
    void* out = nullptr; uint32_t out_len = 0;

    std::vector<unsigned char> u8;
    std::vector<float> f01, f255;
    float mA = 0, mB = 0, mD = 0;
    int cA = 0, cB = 0, cD = 0;
    Det dA{-9, -1}, dB{-9, -1}, dD{-9, -1};

    for (int k = 0; k < N; ++k) {
        const int idx = (total > 1) ? (int)((long long)total * k / N) : 0;
        cap.set(cv::CAP_PROP_POS_FRAMES, idx);
        cv::Mat bgr;
        if (!cap.read(bgr) || bgr.empty()) continue;
        if (k == 0) cv::imwrite("/root/smoke/scene.jpg", bgr);

        letterbox(bgr, u8);
        f01.resize(u8.size()); f255.resize(u8.size());
        for (size_t i = 0; i < u8.size(); ++i) { f01[i] = u8[i] / 255.0f; f255[i] = (float)u8[i]; }

        auto run = [&](void* p, bool native, std::vector<float>& dst) {
            it->set_input_tensor(0, p, native);
            it->invoke();
            out = nullptr; out_len = 0;
            it->get_output_tensor(0, &out, &out_len);
            dst.assign((float*)out, (float*)out + out_len / 4);
        };
        run(u8.data(), true, resA);
        run(f01.data(), false, resB);
        run(f255.data(), false, resD);

        Det a = best(resA.data()), b = best(resB.data()), d = best(resD.data());
        if (a.conf > mA) { mA = a.conf; dA = a; }
        if (b.conf > mB) { mB = b.conf; dB = b; }
        if (d.conf > mD) { mD = d.conf; dD = d; }
        cA += countAbove(resA.data(), 0.3f);
        cB += countAbove(resB.data(), 0.3f);
        cD += countAbove(resD.data(), 0.3f);
        printf("frame %2d/%2d (idx %d): A conf=%.3f  B conf=%.3f  D conf=%.3f\n",
               k + 1, N, idx, a.conf, b.conf, d.conf);
    }

    printf("\n=== summary over %d frames ===\n", N);
    printf("A u8/native : max conf=%.4f, boxes>0.3 = %d, best rec=%d\n", mA, cA, dA.idx);
    printf("B f01/quant : max conf=%.4f, boxes>0.3 = %d, best rec=%d\n", mB, cB, dB.idx);
    printf("D f255/quant: max conf=%.4f, boxes>0.3 = %d, best rec=%d\n", mD, cD, dD.idx);

    auto show = [&](const std::vector<float>& v, const Det& d, const char* tag) {
        if (d.idx < 0 || d.conf < 0.05f) { printf("%s: no detection\n", tag); return; }
        const float* r = v.data() + d.idx * NS;
        printf("%s rec %d: conf=%.3f pts=", tag, d.idx, d.conf);
        for (int i = 0; i < 8; i += 2) printf("(%.1f,%.1f)", r[i], r[i + 1]);
        printf(" col=");
        for (int i = 0; i < 4; ++i) printf("%.2f,", r[9 + i]);
        printf(" cls=");
        for (int i = 0; i < 9; ++i) printf("%.2f,", r[13 + i]);
        printf("\n");
    };
    show(resA, dA, "A");
    show(resB, dB, "B");
    show(resD, dD, "D");
    fflush(stdout);
    _exit(0);
}
