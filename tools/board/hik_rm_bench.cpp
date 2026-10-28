// Measure real camera FPS through OpenRM's Hikvision driver
#include <openrm.h>
#include <cstdio>
#include <chrono>
#include <thread>

int main() {
    int num = 0;
    if (!rm::getHikVisionCameraNum(num) || num < 1) { printf("no camera\n"); return 1; }
    printf("cameras: %d\n", num);

    rm::Camera* cam = new rm::Camera();
    float yaw = 0, pitch = 0, roll = 0;
    if (!rm::openHikVision(cam, 1, &yaw, &pitch, &roll, false, 5000.0, 16.0, 60.0)) {
        printf("open failed\n");
        return 1;
    }
    printf("opened %dx%d\n", cam->width, cam->height);

    using clock = std::chrono::steady_clock;
    auto t0 = clock::now();
    int n = 0, empty = 0;
    double sum_dt = 0;
    auto last = clock::now();
    while (true) {
        auto now = clock::now();
        if (std::chrono::duration<double>(now - t0).count() > 5.0) break;
        auto f = cam->buffer->pop();
        if (!f) { ++empty; std::this_thread::sleep_for(std::chrono::microseconds(200)); continue; }
        auto dt = std::chrono::duration<double, std::milli>(now - last).count();
        last = now;
        if (n > 0) sum_dt += dt;
        ++n;
    }
    printf("frames=%d in 5s -> %.1f fps, avg interval %.2f ms (empty polls %d)\n",
           n, n / 5.0, n > 1 ? sum_dt / (n - 1) : 0.0, empty);

    rm::closeHikVision();
    return 0;
}