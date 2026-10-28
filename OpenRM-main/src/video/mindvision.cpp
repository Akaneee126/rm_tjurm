#include <map>
#include <string>
#include <iostream>
#include "uniterm/uniterm.h"
#include "utils/timer.h"
#include "structure/stamp.hpp"
#include "structure/camera.hpp"
#include "video/video.h"
#include "video/mindvision/CameraApi.h"

using namespace rm;
using namespace std;

constexpr int MAX_CAM_NUM = 5;

#define MV_ASSERT_WARNING(expr, info, ...) do{                       \
    if(!(expr)) {                                                    \
        rm::message("MindVision " + string(info), rm::MSG_WARNING);  \
    }                                                                \
} while(0)

#define MV_ASSERT_ERROR(expr, info, ...) do{                        \   
    if(!(expr)) {                                                   \
        rm::message("MindVision " + string(info), rm::MSG_ERROR);   \
        return false;                                               \
    }                                                               \
} while(0)

#define MV_CHECK_API_WARNING(expr, info, ...) do{                   \
    if(expr != CAMERA_STATUS_SUCCESS) {                             \
        rm::message("MindVision " + string(info), rm::MSG_WARNING); \
    }                                                               \
} while(0)

#define MV_CHECK_API_ERROR(expr, info, ...) do{                     \
    if(expr != CAMERA_STATUS_SUCCESS) {                             \
        rm::message("MindVision " + string(info), rm::MSG_ERROR);   \
        return false;                                               \
    }                                                               \
} while(0)

static tSdkCameraDevInfo infos[MAX_CAM_NUM];
static std::map<int, CameraHandle> camap;
struct CallbackParam {
    Camera* camera;
    float* yaw;
    float* pitch;
    float* roll;
    bool flip = false;
};

void OnFrameCallbackFun(CameraHandle hCamera, BYTE *pFrameBuffer, tSdkFrameHead* pFrameHead, PVOID pContext) {
    TimePoint time_stamp = getTime();
    CallbackParam* callback_param = reinterpret_cast<CallbackParam*>(pContext);
    float yaw = 0, pitch = 0, roll = 0;
    if (callback_param->yaw != nullptr && callback_param->pitch != nullptr && callback_param->roll != nullptr) {
        yaw = *(callback_param->yaw);
        pitch = *(callback_param->pitch);
        roll = *(callback_param->roll);
    }

    Camera *camera = callback_param->camera;
    bool flip = callback_param->flip;
    
    shared_ptr<Frame> frame = make_shared<Frame>();

    frame->image = make_shared<cv::Mat>(camera->height, camera->width, CV_8UC3);
    frame->time_point = time_stamp;
    frame->camera_id = camera->camera_id;
    frame->width = camera->width;
    frame->height = camera->height;
    frame->yaw = yaw;
    frame->pitch = pitch;
    frame->roll = roll;
    
    CameraSdkStatus status = CameraImageProcess(hCamera, pFrameBuffer, frame->image->data, pFrameHead);
    if (status != CAMERA_STATUS_SUCCESS) {
        rm::message("MindVision callback RAW to RGB failed", rm::MSG_ERROR);
        return;
    }
    cv::cvtColor(*(frame->image), *(frame->image), cv::COLOR_RGB2BGR);

    // if (flip) {
    //     cv::rotate(*(frame->image), *(frame->image), 1);
    // }

    camera->buffer->push(frame);
}

bool rm::getMindVisionCameraNum(int& num) {
    MV_CHECK_API_WARNING(CameraSdkInit(0), "lib init faild!");
    num = MAX_CAM_NUM;
    MV_CHECK_API_ERROR(CameraEnumerateDevice(infos, &num), "enumerate device failed!");
    return true;
}

bool rm::setMindVisionArgs(
    Camera *camera,
    double exposure,
    double gain,
    double fps
) {
    CameraHandle hCamera = camap[camera->camera_id];
    MV_CHECK_API_ERROR(CameraSetAeState(hCamera, false), "set manual exposure failed!");
    MV_CHECK_API_ERROR(CameraSetExposureTime(hCamera, exposure), "set exposure failed!");
    MV_CHECK_API_WARNING(CameraSetAnalogGain(hCamera, static_cast<int>(gain)), "set gain failed!");
    MV_CHECK_API_WARNING(CameraSetFrameSpeed(hCamera, static_cast<int>(fps)), "set fps failed!");

    return true;
}

bool rm::openMindVision(
    Camera *camera,
    int device_num,
    float *yaw_ptr,
    float *pitch_ptr,
    float *roll_ptr,
    bool flip,
    bool auto_white_balance,
    double exposure,
    double gain,
    double fps
) {
    // 初始化camera对象
    if(camera == nullptr) {
        rm::message("Video MindVision error at nullptr camera", rm::MSG_ERROR);
        return false;
    }
    if (camera->buffer != nullptr) {
        delete camera->buffer;
    }
    camera->buffer = new SwapBuffer<Frame>();
    camera->camera_id = device_num;

    // 打开设备
    CameraHandle hCamera;
    MV_CHECK_API_ERROR(CameraInit(&infos[device_num - 1], -1, -1, &hCamera), "camera init failed!");

    // 将设备句柄存入map
    camap[device_num] = hCamera;
    
    // 获取图像尺寸
    tSdkImageResolution image_info;
    MV_CHECK_API_ERROR(CameraGetImageResolution(hCamera, &image_info), "get image resolution failed!");

    camera->width = image_info.iWidth;
    camera->height = image_info.iHeight;

    rm::setMindVisionArgs(camera, exposure, gain, fps);

    // 设置图像旋转
    if (flip) {
        CameraSetHardwareMirror(hCamera, 0, true);
        CameraSetHardwareMirror(hCamera, 1, true);
    }

    // 设置回调函数
    
    CallbackParam* callback_param = new CallbackParam;
    callback_param->camera = camera;
    callback_param->yaw = yaw_ptr;
    callback_param->pitch = pitch_ptr;
    callback_param->roll = roll_ptr;
    callback_param->flip = flip;

    MV_CHECK_API_ERROR(CameraSetCallbackFunction(hCamera,
                    OnFrameCallbackFun,
                    reinterpret_cast<void*>(callback_param),
                    NULL), "set callback function failed!");

    // 发送开采命令
    MV_CHECK_API_ERROR(CameraPlay(hCamera), "start acquisition failed!");

    // 设置白平衡
    if(auto_white_balance) {
        MV_CHECK_API_WARNING(CameraSetOnceWB(hCamera), "set white balance failed!");
    }

    rm::message("Video MindVision opened", rm::MSG_OK);

    return true;
}

bool rm::closeMindVision() {
    for (auto& [id, hCamera] : camap) {
        MV_CHECK_API_ERROR(CameraStop(hCamera), "stop acquisition failed!");
        MV_CHECK_API_ERROR(CameraUnInit(hCamera), "uninit camera failed!");
    }
    return true;
}
