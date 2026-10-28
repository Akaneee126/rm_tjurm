// Live camera -> rm::Infer -> NMS, print detections and save annotated frames
#include <openrm.h>
#include <opencv2/opencv.hpp>
#include <cstdio>
#include <vector>
#include <unistd.h>

int main(int argc, char** argv) {
    const char* model = argv[1];
    int seconds = (argc > 2) ? atoi(argv[2]) : 8;

    rm::InferOptions opt;
    opt.input_width = 640; opt.input_height = 640;
    opt.input_layout = rm::InferLayout::NHWC;
    opt.input_type = rm::InferDType::FLOAT32;
    opt.output_bboxes_num = 25200; opt.output_struct_num = 22;
    opt.backend = rm::InferBackend::AIDLITE;

    rm::Infer infer;
    if (!infer.load(model, opt)) { printf("infer load failed\n"); return 1; }
    rm::yoloSetSigmoid(true);
    rm::yoloSetPointOrder(3);

    int num = 0;
    if (!rm::getHikVisionCameraNum(num) || num < 1) { printf("no camera\n"); return 1; }
    rm::Camera* cam = new rm::Camera();
    float yaw = 0, pitch = 0, roll = 0;
    if (!rm::openHikVision(cam, 1, &yaw, &pitch, &roll, false, 20000.0, 16.0, 60.0)) {
        printf("open failed\n"); return 1;
    }
    printf("camera %dx%d\n", cam->width, cam->height);

    using clock = std::chrono::steady_clock;
    auto t0 = clock::now();
    int frames = 0, hit = 0, saved = 0;
    double sum_ms = 0;
    while (std::chrono::duration<double>(clock::now() - t0).count() < seconds) {
        auto f = cam->buffer->pop();
        if (!f) { std::this_thread::sleep_for(std::chrono::microseconds(500)); continue; }
        ++frames;
        auto a = clock::now();
        if (!infer.setInput(*f->image)) continue;
        infer.enqueue();
        const float* out = infer.synchronize();
        if (!out) { printf("infer failed: %s\n", infer.lastError().c_str()); break; }
        auto list = rm::yoloArmorNMS_FPX(const_cast<float*>(out), 25200, 9, 0.3f, 0.02f,
                                         f->width, f->height, 640, 640, 4);
        auto b = clock::now();
        sum_ms += std::chrono::duration<double, std::milli>(b - a).count();

        if (!list.empty()) {
            ++hit;
            if (hit <= 8) {
                const auto& d = list[0];
                printf("det conf=%.3f class=%d color=%d box=(%d,%d %dx%d)\n",
                       d.confidence, d.class_id, d.color_id, d.box.x, d.box.y,
                       d.box.width, d.box.height);
            }
            if (saved < 3) {
                cv::Mat vis = f->image->clone();
                for (const auto& d : list) {
                    std::vector<cv::Point> poly;
                    for (auto& p : d.four_points) poly.push_back(cv::Point((int)p.x, (int)p.y));
                    if (poly.size() == 4) cv::polylines(vis, poly, true, cv::Scalar(0, 255, 0), 4);
                }
                char p[64]; snprintf(p, sizeof(p), "/root/smoke/live_det_%d.jpg", saved++);
                cv::imwrite(p, vis);
                printf("   saved %s\n", p);
            }
        }
    }
    printf("\nframes=%d hits=%d  avg %.2f ms/frame\n", frames, hit, frames ? sum_ms / frames : 0.0);
    rm::closeHikVision();
    fflush(stdout);
    _exit(0);
}