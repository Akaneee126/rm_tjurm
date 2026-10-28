// 真实图像 + 两种输入模式对比 + 分阶段计时
// 编译：g++ -O2 -std=c++17 test2.cpp -I/usr/local/include -L/usr/local/lib -laidlite \
//        $(pkg-config --cflags --libs opencv4) -o test2

#include <aidlux/aidlite/aidlite.hpp>
#include <opencv2/opencv.hpp>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>
#include <unistd.h>

namespace aal = Aidlux::Aidlite;
using namespace aal;

static const int IW = 640, IH = 640, NB = 25200, NS = 22;

static double now_ms() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

// 与 rm::writeTensor 一致的 letterbox：等比 + 居中 + 114
static void letterbox_u8(const cv::Mat& bgr, unsigned char* dst) {
    const int sw_src = bgr.cols, sh_src = bgr.rows;
    const float scale = std::min((float)IW / sw_src, (float)IH / sh_src);
    int sw = std::max(1, (int)lround(sw_src * scale));
    int sh = std::max(1, (int)lround(sh_src * scale));
    const int px = (IW - sw) / 2, py = (IH - sh) / 2;

    cv::Mat canvas(IH, IW, CV_8UC3, cv::Scalar::all(114));
    cv::Mat resized;
    cv::resize(bgr, resized, cv::Size(sw, sh), 0, 0, cv::INTER_LINEAR);
    resized.copyTo(canvas(cv::Rect(px, py, sw, sh)));

    cv::Mat rgb;
    cv::cvtColor(canvas, rgb, cv::COLOR_BGR2RGB);
    std::memcpy(dst, rgb.data, (size_t)IW * IH * 3);   // NHWC uint8
}

static void letterbox_f32(const cv::Mat& bgr, float* dst) {
    std::vector<unsigned char> u8((size_t)IW * IH * 3);
    letterbox_u8(bgr, u8.data());
    for (size_t i = 0; i < u8.size(); ++i) dst[i] = u8[i] / 255.0f;   // NHWC float
}

static void report(const float* out, const char* tag) {
    float best = -1e9f;
    int bi = -1;
    for (int i = 0; i < NB; ++i) {
        float c = out[i * NS + 8];
        if (c > best) { best = c; bi = i; }
    }
    printf("[%s] max conf=%.4f @ rec %d\n", tag, best, bi);
    if (bi < 0) return;
    const float* r = out + bi * NS;
    printf("   points:");
    for (int k = 0; k < 8; k += 2) printf(" (%.1f,%.1f)", r[k], r[k + 1]);
    printf("\n   colors:");
    for (int k = 0; k < 4; ++k) printf(" %.3f", r[9 + k]);
    printf("\n   classes:");
    for (int k = 0; k < 9; ++k) printf(" %.3f", r[13 + k]);
    printf("\n");
}

int main(int argc, char** argv) {
    const char* model = argv[1];

    cv::Mat bgr;
    cv::VideoCapture cap(0, cv::CAP_V4L2);
    if (cap.isOpened()) {
        cap.set(cv::CAP_PROP_FRAME_WIDTH, 1280);
        cap.set(cv::CAP_PROP_FRAME_HEIGHT, 1024);
        cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
        for (int i = 0; i < 5; ++i) cap >> bgr;      // 丢几帧等自动曝光稳定
    }
    if (bgr.empty()) {
        printf("camera unavailable, fallback to decal\n");
        bgr = cv::imread("/root/smoke/armor1.png");
    }
    if (bgr.empty()) { printf("FAIL: no image\n"); return 1; }
    printf("image: %dx%d\n", bgr.cols, bgr.rows);
    cv::imwrite("/root/smoke/frame.jpg", bgr);

    Model* m = Model::create_instance(model);
    Config* c = Config::create_instance();
    c->framework_type = FrameworkType::TYPE_QNN;
    c->accelerate_type = AccelerateType::TYPE_DSP;
    c->implement_type = ImplementType::TYPE_LOCAL;
    auto it = InterpreterBuilder::build_interpreter_from_model_and_config(m, c);
    if (!it || it->init() != 0 || it->load_model() != 0) { printf("FAIL: load\n"); return 1; }

    std::vector<unsigned char> buf_u8((size_t)IW * IH * 3);
    std::vector<float> buf_f32((size_t)IW * IH * 3);
    letterbox_u8(bgr, buf_u8.data());
    letterbox_f32(bgr, buf_f32.data());
    cv::Mat vis(IH, IW, CV_8UC3, buf_u8.data());
    cv::imwrite("/root/smoke/letterbox.png", vis);

    void* out = nullptr;
    uint32_t out_len = 0;

    // ---- 模式 A：uint8 + is_native=true ----
    double t0 = now_ms();
    for (int i = 0; i < 3; ++i) {
        it->set_input_tensor(0, buf_u8.data(), true);
        it->invoke();
    }
    double t_setA = 0, t_invA = 0, t_getA = 0;
    std::vector<float> resA;
    for (int i = 0; i < 10; ++i) {
        double a = now_ms();
        it->set_input_tensor(0, buf_u8.data(), true);
        double b = now_ms();
        it->invoke();
        double d = now_ms();
        it->get_output_tensor(0, &out, &out_len);
        double e = now_ms();
        t_setA += b - a; t_invA += d - b; t_getA += e - d;
        if (i == 9) resA.assign((float*)out, (float*)out + (out_len / 4));
    }
    printf("A uint8/native : set=%.2f invoke=%.2f get=%.2f total=%.2f ms\n",
           t_setA / 10, t_invA / 10, t_getA / 10, (t_setA + t_invA + t_getA) / 10);
    report(resA.data(), "A uint8/native");

    // ---- 模式 B：float32 + is_native=false（SDK 内部量化）----
    double t_setB = 0, t_invB = 0, t_getB = 0;
    std::vector<float> resB;
    for (int i = 0; i < 3; ++i) {
        it->set_input_tensor(0, buf_f32.data(), false);
        it->invoke();
    }
    for (int i = 0; i < 10; ++i) {
        double a = now_ms();
        it->set_input_tensor(0, buf_f32.data(), false);
        double b = now_ms();
        it->invoke();
        double d = now_ms();
        it->get_output_tensor(0, &out, &out_len);
        double e = now_ms();
        t_setB += b - a; t_invB += d - b; t_getB += e - d;
        if (i == 9) resB.assign((float*)out, (float*)out + (out_len / 4));
    }
    printf("B f32/nonative : set=%.2f invoke=%.2f get=%.2f total=%.2f ms\n",
           t_setB / 10, t_invB / 10, t_getB / 10, (t_setB + t_invB + t_getB) / 10);
    report(resB.data(), "B f32/nonative");

    // 两种模式结果差异（应当很小）
    double diff = 0, maxd = 0;
    for (size_t i = 0; i < resA.size() && i < resB.size(); ++i) {
        double d = std::fabs(resA[i] - resB[i]);
        diff += d; if (d > maxd) maxd = d;
    }
    printf("A vs B: mean|diff|=%.6f max|diff|=%.3f  (n=%zu)\n",
           diff / (double)resA.size(), maxd, resA.size());

    fflush(stdout);
    _exit(0);   // AidLite 析构会挂住，直接退出
}
