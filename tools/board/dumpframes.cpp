#include <opencv2/opencv.hpp>
#include <cstdio>
#include <unistd.h>

int main(int argc, char** argv) {
    const char* video = (argc > 1) ? argv[1] : "/root/smoke/demo.avi";
    cv::VideoCapture cap(video);
    if (!cap.isOpened()) { printf("FAIL open\n"); return 1; }
    printf("size %dx%d fps %.1f count %.1f\n",
           (int)cap.get(cv::CAP_PROP_FRAME_WIDTH),
           (int)cap.get(cv::CAP_PROP_FRAME_HEIGHT),
           cap.get(cv::CAP_PROP_FPS),
           cap.get(cv::CAP_PROP_FRAME_COUNT));

    const int want[] = {0, 60, 120, 240, 400, 600};
    int wi = 0, n = 0;
    cv::Mat img;
    while (cap.read(img) && !img.empty()) {
        if (wi < 6 && n == want[wi]) {
            char p[64];
            snprintf(p, sizeof(p), "/root/smoke/f%d.jpg", n);
            cv::imwrite(p, img);
            cv::Scalar m = cv::mean(img);
            printf("saved %s  mean=%.1f (B %.1f G %.1f R %.1f)\n", p, (m[0]+m[1]+m[2])/3.0, m[0], m[1], m[2]);
            ++wi;
        }
        ++n;
    }
    printf("total frames read: %d\n", n);
    _exit(0);
}
