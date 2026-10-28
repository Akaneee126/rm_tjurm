// A/B: flip_mode 0 vs 3, same scene, count detections
#include <openrm.h>
#include <opencv2/opencv.hpp>
#include <cstdio>
#include <vector>
#include <unistd.h>

static rm::Infer* g_infer = nullptr;

static void run(rm::Camera* cam, int seconds, const char* tag) {
    using clock = std::chrono::steady_clock;
    auto t0 = clock::now();
    int frames = 0, hits = 0; float best = 0;
    while (std::chrono::duration<double>(clock::now() - t0).count() < seconds) {
        auto f = cam->buffer->pop();
        if (!f) { std::this_thread::sleep_for(std::chrono::microseconds(500)); continue; }
        ++frames;
        if (!g_infer->setInput(*f->image)) continue;
        g_infer->enqueue();
        const float* out = g_infer->synchronize();
        if (!out) break;
        auto list = rm::yoloArmorNMS_FPX(const_cast<float*>(out), 25200, 9, 0.3f, 0.02f,
                                         f->width, f->height, 640, 640, 4);
        if (!list.empty()) {
            ++hits;
            if (list[0].confidence > best) best = list[0].confidence;
            if (hits == 1) cv::imwrite(std::string("/root/smoke/ab_") + tag + ".jpg", *f->image);
        }
    }
    printf("[%s] frames=%d hits=%d (%.0f%%) best_conf=%.3f\n",
           tag, frames, hits, frames ? 100.0 * hits / frames : 0.0, best);
}

int main(int argc, char** argv) {
    rm::InferOptions opt;
    opt.input_width = 640; opt.input_height = 640;
    opt.input_layout = rm::InferLayout::NHWC;
    opt.input_type = rm::InferDType::FLOAT32;
    opt.output_bboxes_num = 25200; opt.output_struct_num = 22;
    opt.backend = rm::InferBackend::AIDLITE;
    rm::Infer infer;
    if (!infer.load(argv[1], opt)) return 1;
    g_infer = &infer;
    rm::yoloSetSigmoid(true);
    rm::yoloSetPointOrder(3);

    int num = 0;
    rm::getHikVisionCameraNum(num);
    rm::Camera* cam = new rm::Camera();
    float y = 0, p = 0, r = 0;
    if (!rm::openHikVision(cam, 1, &y, &p, &r, false, 20000.0, 16.0, 60.0)) return 1;

    cam->flip_mode = 0;
    run(cam, 5, "flip0");
    cam->flip_mode = 3;
    run(cam, 5, "flip3");
    cam->flip_mode = 1;
    run(cam, 5, "flip1");

    rm::closeHikVision();
    fflush(stdout);
    _exit(0);
}