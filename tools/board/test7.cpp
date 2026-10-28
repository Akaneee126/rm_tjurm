// (1) 閲?CPU letterbox 鑰楁椂  (2) 鐪嬭緭鍑哄紶閲忕殑"鍘熷"鏁存暟鍊硷紝纭閲忓寲姝ラ暱
#include <aidlux/aidlite/aidlite.hpp>
#include <opencv2/opencv.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
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

int main(int argc, char** argv) {
    const char* model = argv[1];
    cv::Mat bgr = cv::imread("/root/smoke/f120.jpg");
    if (bgr.empty()) { printf("no img\n"); return 1; }

    // ---------- (1) letterbox 鍩哄噯 ----------
    cv::Mat canvas(IH, IW, CV_8UC3);
    cv::Mat rgb;
    std::vector<unsigned char> out_u8((size_t)IW * IH * 3);
    double t0 = now_ms();
    const int N = 100;
    for (int i = 0; i < N; ++i) {
        const float sc = std::min((float)IW / bgr.cols, (float)IH / bgr.rows);
        int sw = std::max(1, (int)lround(bgr.cols * sc)), sh = std::max(1, (int)lround(bgr.rows * sc));
        cv::Mat rs; cv::resize(bgr, rs, cv::Size(sw, sh));
        canvas.setTo(cv::Scalar::all(114));
        rs.copyTo(canvas(cv::Rect((IW - sw) / 2, (IH - sh) / 2, sw, sh)));
        cv::cvtColor(canvas, rgb, cv::COLOR_BGR2RGB);
        std::memcpy(out_u8.data(), rgb.data, out_u8.size());
    }
    double tlb = (now_ms() - t0) / N;
    printf("letterbox 1440x1080 -> 640x640 NHWC u8: %.2f ms  (%d iters)\n", tlb, N);

    // ---------- (2) 鍘熷閲忓寲鍊?----------
    Model* m = Model::create_instance(model);
    Config* c = Config::create_instance();
    c->framework_type = FrameworkType::TYPE_QNN;
    c->accelerate_type = AccelerateType::TYPE_DSP;
    c->implement_type = ImplementType::TYPE_LOCAL;
    auto it = InterpreterBuilder::build_interpreter_from_model_and_config(m, c);
    if (!it || it->init() != 0 || it->load_model() != 0) { printf("FAIL\n"); return 1; }

    it->set_input_tensor(0, out_u8.data(), true);
    it->invoke();

    void* out = nullptr; uint32_t len = 0;
    int rc = it->get_output_tensor(0, &out, true, &len);   // is_native = true
    printf("\nraw output: rc=%d len=%u bytes\n", rc, len);
    if (rc == 0 && out) {
        const unsigned char* raw = (const unsigned char*)out;
        int mn = 255, mx = 0; long sum = 0; int hist[256] = {0};
        size_t n = len;   // 1 byte per element
        if (n > 554400) n = 554400;
        for (size_t i = 0; i < n; ++i) {
            int v = raw[i];
            hist[v]++;
            if (v < mn) mn = v;
            if (v > mx) mx = v;
            sum += v;
        }
        printf("raw u8 range [%d, %d], mean=%.1f\n", mn, mx, (double)sum / n);
        printf("histogram of top values:");
        for (int v = 255; v >= 0 && v > mx - 12; --v) printf(" %d:%d", v, hist[v]);
        printf("\n");
        // 鎵撳嵃绗?0 鏉¤褰曠殑 22 涓師濮嬪瓧鑺?        printf("rec0 raw:");
        for (int k = 0; k < NS; ++k) printf(" %d", raw[k]);
        printf("\n");
    }
    fflush(stdout);
    _exit(0);
}
