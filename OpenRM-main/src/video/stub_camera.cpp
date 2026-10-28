// 当某种工业相机驱动没有安装时（例如 AidLux 犀牛派上只有 UVC 摄像头），
// 提供同名空实现，保证 openrm_video / TJURM 仍然可以正常链接。
//
// 每个函数由对应驱动的 CMake 检测宏控制，避免与真实实现重复定义。

#include "video/video.h"
#include "uniterm/uniterm.h"

// ---------------------------------------------------------------------------
#ifndef HAVE_MVSDK
bool rm::getMindVisionCameraNum(int& device_num) {
    device_num = 0;
    rm::message("MindVision SDK not installed, driver disabled", rm::MSG_WARNING);
    return false;
}
bool rm::setMindVisionArgs(rm::Camera*, double, double, double) { return false; }
bool rm::openMindVision(rm::Camera*, int, float*, float*, float*, bool, bool, double, double, double) { return false; }
bool rm::closeMindVision() { return true; }
#endif

// ---------------------------------------------------------------------------
#ifndef HAVE_GXIAPI
bool rm::getDaHengCameraNum(int& device_num) {
    device_num = 0;
    rm::message("DaHeng SDK not installed, driver disabled", rm::MSG_WARNING);
    return false;
}
bool rm::setDaHengArgs(rm::Camera*, double, double, double) { return false; }
bool rm::openDaHeng(rm::Camera*, int, float*, float*, float*, bool, double, double, double) { return false; }
bool rm::closeDaHeng() { return true; }
#endif

// ---------------------------------------------------------------------------
#ifndef HAVE_MVCC
bool rm::getHikVisionCameraNum(int& device_num) {
    device_num = 0;
    rm::message("HikVision SDK not installed, driver disabled", rm::MSG_WARNING);
    return false;
}
bool rm::setHikVisionArgs(rm::Camera*, double, double, double) { return false; }
bool rm::openHikVision(rm::Camera*, int, float*, float*, float*, bool, double, double, double) { return false; }
bool rm::closeHikVision() { return true; }
#endif
