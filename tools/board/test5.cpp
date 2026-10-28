// 顺序解码视频（该 AVI 的 FRAME_COUNT 不可用，不能 seek），逐帧推理并保存样本帧
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

static void letterbox(const cv::Mat& bgr, std::vector<unsigned char>& u8, std::vector<float>& f01) {
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
}

struct Det { float conf; int idx; };

int main(int argc, char** argv) {
    const char* model = argv[1];
    const char* video = (argc > 2) ? argv[2] : "/root/smoke/demo.avi";
    const int every = (argc > 3) ? atoi(argv[3]) : 25;

    cv::VideoCapture cap(video);
    if (!cap.isOpened()) { printf("FAIL open\n"); return 1; }

    Model* m = Model::create_instance(model);
    Config* c = Config::create_instance();
    c->framework_type = FrameworkType::TYPE_QNN;
    c->accelerate_type = AccelerateType::TYPE_DSP;
    c->implement_type = ImplementType::TYPE_LOCAL;
    auto it = InterpreterBuilder::build_interpreter_from_model_and_config(m, c);
    if (!it || it->init() != 0 || it->load_model() != 0) { printf("FAIL load\n"); return 1; }

    std::vector<unsigned char> u8;
    std::vector<float> f01, outA, outB;
    void* out = nullptr; uint32_t out_len = 0;
    cv::Mat img;

    int n = 0, tested = 0, saved = 0;
    float bestA = 0, bestB = 0;
    int bestAn = -1, bestBn = -1;

    while (cap.read(img) && !img.empty() && n < 2000) {
        if (n % every != 0) { ++n; continue; }
        letterbox(img, u8, f01);

        auto run = [&](void* p, bool native, std::vector<float>& dst) {
            it->set_input_tensor(0, p, native);
            int rc = it->invoke();
            if (rc != 0) { printf("invoke rc=%d\n", rc); }
            out = nullptr; out_len = 0;
            it->get_output_tensor(0, &out, &out_len);
            dst.assign((float*)out, (float*)out + out_len / 4);
        };
        run(u8.data(), true, outA);
        run(f01.data(), false, outB);

        float mA = -1e9f, mB = -1e9f;
        int iA = -1, iB = -1;
        for (int i = 0; i < NB; ++i) {
            if (outA[i * NS + 8] > mA) { mA = outA[i * NS + 8]; iA = i; }
            if (outB[i * NS + 8] > mB) { mB = outB[i * NS + 8]; iB = i; }
        }
        printf("frame %4d: A(u8/native) conf=%.3f | B(f01/quant) conf=%.3f\n", n, mA, mB);
        if (mA > bestA) { bestA = mA; bestAn = n; }
        if (mB > bestB) { bestB = mB; bestBn = n; }

        if (mA > 0.3f && saved < 3) {
            char p[64];
            snprintf(p, sizeof(p), "/root/smoke/det_%d.jpg", saved);
            cv::Mat vis = img.clone();
            const float* r = outA.data() + iA * NS;
            printf("   -> A det: conf=%.3f pts=", mA);
            for (int k = 0; k < 8; k += 2) printf("(%.1f,%.1f)", r[k], r[k + 1]);
            printf(" col=");
            for (int k = 0; k < 4; ++k) printf("%.2f,", r[9 + k]);
            printf(" cls=");
            for (int k = 0; k < 9; ++k) printf("%.2f,", r[13 + k]);
            printf("\n");
            const float sc = std::min(640.0f / img.cols, 640.0f / img.rows);
            const int sw = (int)lround(img.cols * sc), sh = (int)lround(img.rows * sc);
            const int px = (640 - sw) / 2, py = (640 - sh) / 2;
            const float inv = 1.0f / sc;
            std::vector<cv::Point> poly;
            for (int k = 0; k < 4; ++k) {
                float X = (r[k * 2] - px) * inv, Y = (r[k * 2 + 1] - py) * inv;
                poly.push_back(cv::Point((int)X, (int)Y));
            }
            cv::polylines(vis, poly, true, cv::Scalar(0, 255, 0), 3);
            cv::imwrite(p, vis);
            ++saved;
        }
        ++tested;
        ++n;
        if (tested >= 60) break;
    }

    printf("\nscanned %d frames, tested %d\n", n, tested);
    printf("best A conf=%.3f @ frame %d\n", bestA, bestAn);
    printf("best B conf=%.3f @ frame %d\n", bestB, bestBn);
    fflush(stdout);
    _exit(0);
}
