// End-to-end check through the PRODUCTION classes: rm::Infer + rm::yoloArmorNMS_FPX
#include <openrm.h>
#include <opencv2/opencv.hpp>
#include <cstdio>
#include <vector>
#include <unistd.h>

int main(int argc, char** argv) {
    const char* model = argv[1];
    const char* video = (argc > 2) ? argv[2] : "/root/smoke/demo.avi";

    rm::InferOptions opt;
    opt.input_width = 640;
    opt.input_height = 640;
    opt.input_layout = rm::InferLayout::NHWC;
    opt.input_type = rm::InferDType::FLOAT32;
    opt.output_bboxes_num = 25200;
    opt.output_struct_num = 22;
    opt.backend = rm::InferBackend::AIDLITE;
    opt.framework = "QNN";
    opt.accelerate = "DSP";

    rm::Infer infer;
    if (!infer.load(model, opt)) { printf("load failed\n"); return 1; }

    rm::yoloSetSigmoid(true);   // 模型输出是 logits

    cv::VideoCapture cap(video);
    if (!cap.isOpened()) { printf("no video\n"); return 1; }

    cv::Mat img;
    int n = 0, hits = 0, tested = 0, saved = 0;
    while (cap.read(img) && !img.empty() && n < 900) {
        if (n % 20 == 0) {
            if (!infer.setInput(img)) { printf("setInput failed\n"); return 1; }
            infer.enqueue();
            const float* out = infer.synchronize();
            if (!out) { printf("infer failed: %s\n", infer.lastError().c_str()); return 1; }

            auto list = rm::yoloArmorNMS_FPX(const_cast<float*>(out), 25200, 9, 0.3f, 0.02f,
                                             img.cols, img.rows, 640, 640, 4);
            ++tested;
            if (!list.empty()) {
                ++hits;
                const auto& b = list[0];
                printf("frame %3d: %zu det | best conf=%.3f class=%d color=%d box=(%d,%d %dx%d)\n",
                       n, list.size(), b.confidence, b.class_id, b.color_id,
                       b.box.x, b.box.y, b.box.width, b.box.height);
                if (saved < 3) {
                    cv::Mat vis = img.clone();
                    for (const auto& d : list) {
                        std::vector<cv::Point> poly;
                        for (auto& p : d.four_points) poly.push_back(cv::Point((int)p.x, (int)p.y));
                        if (poly.size() == 4) cv::polylines(vis, poly, true, cv::Scalar(0, 255, 0), 4);
                    }
                    char p[64];
                    snprintf(p, sizeof(p), "/root/smoke/prod_%d.jpg", saved++);
                    cv::imwrite(p, vis);
                    printf("          saved %s\n", p);
                }
            } else {
                printf("frame %3d: no det\n", n);
            }
        }
        ++n;
    }
    printf("\n%d/%d frames with detection\n", hits, tested);
    fflush(stdout);
    _exit(0);
}