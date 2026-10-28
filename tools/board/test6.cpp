// 逐通道统计输出（判定 22 通道布局 / 输入编码是否把网络"喂活"）
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

int main(int argc, char** argv) {
    const char* model = argv[1];
    const char* img_path = (argc > 2) ? argv[2] : "/root/smoke/f120.jpg";

    cv::Mat bgr = cv::imread(img_path);
    if (bgr.empty()) { printf("no image %s\n", img_path); return 1; }
    printf("image %dx%d\n", bgr.cols, bgr.rows);

    // letterbox
    std::vector<unsigned char> u8((size_t)IW * IH * 3, 114);
    {
        const float sc = std::min((float)IW / bgr.cols, (float)IH / bgr.rows);
        int sw = std::max(1, (int)lround(bgr.cols * sc)), sh = std::max(1, (int)lround(bgr.rows * sc));
        cv::Mat rs; cv::resize(bgr, rs, cv::Size(sw, sh));
        cv::Mat canvas(IH, IW, CV_8UC3, cv::Scalar::all(114));
        rs.copyTo(canvas(cv::Rect((IW - sw) / 2, (IH - sh) / 2, sw, sh)));
        cv::Mat rgb; cv::cvtColor(canvas, rgb, cv::COLOR_BGR2RGB);
        std::memcpy(u8.data(), rgb.data, (size_t)IW * IH * 3);
        cv::imwrite("/root/smoke/lb.jpg", rgb);
    }
    std::vector<float> f01(u8.size()), f255(u8.size());
    for (size_t i = 0; i < u8.size(); ++i) { f01[i] = u8[i] / 255.0f; f255[i] = (float)u8[i]; }

    Model* m = Model::create_instance(model);
    Config* c = Config::create_instance();
    c->framework_type = FrameworkType::TYPE_QNN;
    c->accelerate_type = AccelerateType::TYPE_DSP;
    c->implement_type = ImplementType::TYPE_LOCAL;
    auto it = InterpreterBuilder::build_interpreter_from_model_and_config(m, c);
    if (!it || it->init() != 0 || it->load_model() != 0) { printf("FAIL load\n"); return 1; }

    void* out = nullptr; uint32_t out_len = 0;

    auto dump = [&](const char* tag, void* p, bool native) {
        it->set_input_tensor(0, p, native);
        int rc = it->invoke();
        out = nullptr; out_len = 0;
        it->get_output_tensor(0, &out, &out_len);
        const float* v = (const float*)out;
        const int n = (int)(out_len / 4);
        printf("\n== %s (rc=%d, %d floats) ==\n", tag, rc, n);
        for (int ch = 0; ch < NS; ++ch) {
            float mx = -1e30f, mn = 1e30f; double sum = 0; int cnt = 0;
            for (int i = 0; i < NB; i += 7) {
                float x = v[i * NS + ch];
                if (x > mx) mx = x;
                if (x < mn) mn = x;
                sum += x; ++cnt;
            }
            printf("  ch%2d: min=%9.3f max=%9.3f mean=%9.3f\n", ch, mn, mx, sum / cnt);
        }
        // 找 ch8 最大的一条记录，打印全 22 个值
        int bi = -1; float bv = -1e30f;
        for (int i = 0; i < NB; ++i) if (v[i * NS + 8] > bv) { bv = v[i * NS + 8]; bi = i; }
        printf("  best-by-ch8 rec %d: ", bi);
        for (int k = 0; k < NS; ++k) printf("%.2f ", v[bi * NS + k]);
        printf("\n");
    };

    dump("A uint8 native", u8.data(), true);
    dump("B f32 0-1 quant", f01.data(), false);
    dump("D f32 0-255 quant", f255.data(), false);
    fflush(stdout);
    _exit(0);
}
