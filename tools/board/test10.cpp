// Try both point orders and check the quad is convex (no self-intersection)
#include <openrm.h>
#include <opencv2/opencv.hpp>
#include <cstdio>
#include <vector>
#include <unistd.h>

static bool isConvex(const std::vector<cv::Point2f>& p) {
    if (p.size() != 4) return false;
    float s = 0;
    for (int i = 0; i < 4; ++i) {
        cv::Point2f a = p[(i + 1) % 4] - p[i];
        cv::Point2f b = p[(i + 2) % 4] - p[(i + 1) % 4];
        float c = a.x * b.y - a.y * b.x;
        if (s == 0) s = c;
        else if (c * s < 0) return false;
    }
    return true;
}

int main(int argc, char** argv) {
    const char* model = argv[1];
    const char* video = (argc > 2) ? argv[2] : "/root/smoke/demo.avi";

    rm::InferOptions opt;
    opt.input_width = 640; opt.input_height = 640;
    opt.input_layout = rm::InferLayout::NHWC;
    opt.input_type = rm::InferDType::FLOAT32;
    opt.output_bboxes_num = 25200; opt.output_struct_num = 22;
    opt.backend = rm::InferBackend::AIDLITE;

    rm::Infer infer;
    if (!infer.load(model, opt)) return 1;
    rm::yoloSetSigmoid(true);

    cv::VideoCapture cap(video);
    if (!cap.isOpened()) return 1;

    cv::Mat img;
    int n = 0, saved = 0;
    while (cap.read(img) && !img.empty() && n < 400) {
        if (n % 40 == 0) {
            infer.setInput(img); infer.enqueue();
            const float* out = infer.synchronize();
            if (!out) return 1;

            for (int mode = 3; mode <= 3; ++mode) {
                rm::yoloSetPointOrder(mode);
                auto list = rm::yoloArmorNMS_FPX(const_cast<float*>(out), 25200, 9, 0.3f, 0.02f,
                                                 img.cols, img.rows, 640, 640, 4);
                if (list.empty()) { printf("frame %3d mode %d: no det\n", n, mode); continue; }
                const auto& d = list[0];
                printf("frame %3d mode %d: conf=%.3f convex=%d pts=", n, mode, d.confidence,
                       (int)isConvex(d.four_points));
                for (auto& p : d.four_points) printf("(%.0f,%.0f)", p.x, p.y);
                printf("\n");
                if (mode == 3 && saved < 2) {
                    cv::Mat vis = img.clone();
                    std::vector<cv::Point> poly;
                    for (auto& p : d.four_points) poly.push_back(cv::Point((int)p.x, (int)p.y));
                    cv::polylines(vis, poly, true, cv::Scalar(0, 255, 0), 4);
                    char path[64]; snprintf(path, sizeof(path), "/root/smoke/ord_%d.jpg", saved++);
                    cv::imwrite(path, vis);
                    printf("   saved %s\n", path);
                }
            }
        }
        ++n;
    }
    fflush(stdout);
    _exit(0);
}