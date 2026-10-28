// exposure sweep with flip=3
#include <openrm.h>
#include <opencv2/opencv.hpp>
#include <cstdio>
#include <vector>
#include <unistd.h>

static rm::Infer* g = nullptr;

static void run(rm::Camera* cam, int seconds, const char* tag, const char* shot) {
    using clock = std::chrono::steady_clock;
    auto t0 = clock::now();
    int frames = 0, hits = 0; float best = 0; bool saved = false;
    while (std::chrono::duration<double>(clock::now() - t0).count() < seconds) {
        auto f = cam->buffer->pop();
        if (!f) { std::this_thread::sleep_for(std::chrono::microseconds(500)); continue; }
        ++frames;
        if (!saved) { cv::imwrite(std::string("/root/smoke/exp_") + tag + ".jpg", *f->image); saved = true; }
        if (!g->setInput(*f->image)) continue;
        g->enqueue();
        const float* out = g->synchronize();
        if (!out) break;
        auto list = rm::yoloArmorNMS_FPX(const_cast<float*>(out), 25200, 9, 0.3f, 0.02f,
                                         f->width, f->height, 640, 640, 4);
        if (!list.empty()) { ++hits; if (list[0].confidence > best) best = list[0].confidence; }
    }
    printf("[%s] frames=%d hits=%d best_conf=%.3f\n", tag, frames, hits, best);
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
    g = &infer;
    rm::yoloSetSigmoid(true);
    rm::yoloSetPointOrder(3);

    int num = 0;
    rm::getHikVisionCameraNum(num);
    rm::Camera* cam = new rm::Camera();
    float y = 0, p = 0, r = 0;
    if (!rm::openHikVision(cam, 1, &y, &p, &r, false, 5000.0, 16.0, 60.0)) return 1;
    cam->flip_mode = 3;

    double exps[] = {1000.0, 3000.0, 8000.0, 20000.0};
    char tag[32];
    for (int i = 0; i < 4; ++i) {
        rm::setHikVisionArgs(cam, exps[i], 16.0, 60.0);
        usleep(500000);
        snprintf(tag, sizeof(tag), "exp%.0f", exps[i]);
        run(cam, 4, tag, tag);
    }
    rm::closeHikVision();
    fflush(stdout);
    _exit(0);
}