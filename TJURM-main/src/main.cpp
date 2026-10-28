#include "data_manager/base.h"
#include "data_manager/param.h"
#include "threads/pipeline.h"
#include "threads/control.h"
#include "garage/garage.h"

#include <condition_variable>
#include <mutex>
#include <thread>
#include <iostream>
#include <chrono>
#include <unistd.h>


std::mutex hang_up_mutex;
std::condition_variable hang_up_cv;

int main(int argc, char** argv) {
    auto param = Param::get_instance();
    auto pipeline = Pipeline::get_instance();
    auto garage = Garage::get_instance();
    auto control = Control::get_instance();

    // OpenCV 内部线程数：AidLux 上 8 核 A78/A55 异构，开太多反而抢 CPU，
    // 默认 4，可在 Config.json Debug.System.CvThreads 调整（<=0 表示用 OpenCV 默认）
    {
        int cv_threads = 4;
        if ((*param)["Debug"]["System"].contains("CvThreads") &&
            (*param)["Debug"]["System"]["CvThreads"].is_number()) {
            cv_threads = (*param)["Debug"]["System"]["CvThreads"].get<int>();
        }
        if (cv_threads > 0) cv::setNumThreads(cv_threads);
    }

    int option;
    while ((option = getopt(argc, argv, "hs")) != -1) {
        switch (option) {
            case 's':
                Data::imshow_flag = true;
                break;
            case 'h':
                std::cout << "Usage: " << argv[0] << " [-h] [-s] " << std::endl;
                break;
        }
    }
    
    while(true) if(init_camera()) break;

    rm::message_init("autoaim");
    init_debug();
    init_attack();
    if (Data::serial_flag) init_serial();
    control->autoaim();

    #if defined(TJURM_INFANTRY) || defined(TJURM_BALANCE)
    // pipeline->autoaim_combine();  
    pipeline->autoaim_baseline();
    #endif

    #if defined(TJURM_SENTRY) || defined(TJURM_DRONSE) || defined(TJURM_HERO)
    pipeline->autoaim_baseline();
    #endif

    while(Data::manu_fire) {
        std::cin.get();
        Data::auto_fire = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        Data::auto_fire = false;
    }

    rm::message("Main thread hang up!", rm::MSG_OK);
    std::unique_lock<std::mutex> lock(hang_up_mutex);
    hang_up_cv.wait(lock);
    return 0;
}
