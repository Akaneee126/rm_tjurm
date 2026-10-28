#include <map>
#include <memory>
#include <iostream>
#include <dlfcn.h>
#include "uniterm/uniterm.h"
#include "utils/timer.h"
#include "structure/stamp.hpp"
#include "structure/camera.hpp"
#include "video/video.h"
#include "video/hikvision/MvCameraControl.h"

// MV_CC_Initialize / MV_CC_Finalize only exist in newer MVS SDK builds.
// The 3.x libMvCameraControl.so used on this board does not export them,
// so call them through dlsym() to stay compatible with both old and new SDKs.
static void hikInitializeOptional() {
    typedef int (*InitFn)();
    InitFn fn = reinterpret_cast<InitFn>(dlsym(RTLD_DEFAULT, "MV_CC_Initialize"));
    if (fn != nullptr) fn();
}

static void hikFinalizeOptional() {
    typedef int (*InitFn)();
    InitFn fn = reinterpret_cast<InitFn>(dlsym(RTLD_DEFAULT, "MV_CC_Finalize"));
    if (fn != nullptr) fn();
}

using namespace rm;
using namespace std;

#define MV_ASSERT_WARNING(expr, info, ...) do{                       \
    if(!(expr)) {                                                    \
        rm::message("Hikvision " + string(info), rm::MSG_WARNING);  \
    }                                                                \
} while(0)

#define MV_ASSERT_ERROR(expr, info, ...) do{                         \
    if(!(expr)) {                                                    \
        rm::message("Hikvision " + string(info), rm::MSG_ERROR);    \
        return false;                                                \
    }                                                                \
} while(0)

#define MV_CHECK_API_WARNING(expr, info, ...) do{                    \
    if(expr != MV_OK) {                                              \
        rm::message("Hikvision " + string(info), rm::MSG_WARNING);  \
    }                                                                \
} while(0)

#define MV_CHECK_API_ERROR(expr, info, ...) do{                      \
    if(expr != MV_OK) {                                              \
        rm::message("Hikvision " + string(info), rm::MSG_ERROR);    \
        return false;                                                \
    }                                                                \
} while(0)

struct CallbackParam {
    Camera* camera;
    float* yaw;
    float* pitch;
    float* roll;
    bool flip = false;
};

static std::map<int, void*> hikvision_map;

static void onFrameCallback(unsigned char* pData, MV_FRAME_OUT_INFO_EX* pFrameInfo, void* pUser)
{
    TimePoint time_stamp = getTime();
    CallbackParam* callback_param = reinterpret_cast<CallbackParam*>(pUser);
    if (!callback_param || !callback_param->camera) {
        rm::message("Hikvision onFrameCallback error at nullptr camera", rm::MSG_ERROR);
        return;
    }
    float yaw = 0.f, pitch = 0.f, roll = 0.f;
    if (callback_param->yaw && callback_param->pitch && callback_param->roll) {
        yaw = *callback_param->yaw;
        pitch = *callback_param->pitch;
        roll = *callback_param->roll;
    }

    std::shared_ptr<Frame> frame = std::make_shared<Frame>();
    frame->camera_id = callback_param->camera->camera_id;
    frame->width = (int)pFrameInfo->nWidth;
    frame->height = (int)pFrameInfo->nHeight;
    frame->yaw = yaw;
    frame->pitch = pitch;
    frame->roll = roll;
    frame->time_point = time_stamp;
    frame->image = std::make_shared<cv::Mat>(frame->height, frame->width, CV_8UC3);

    // MVS 3.x API: MV_CC_PIXEL_CONVERT_PARAM + MV_CC_ConvertPixelType
    // (the *_EX variants only exist from 4.x on)
    MV_CC_PIXEL_CONVERT_PARAM cvtParam = {0};
    cvtParam.nWidth         = (unsigned short)pFrameInfo->nWidth;
    cvtParam.nHeight        = (unsigned short)pFrameInfo->nHeight;
    cvtParam.pSrcData       = pData;
    cvtParam.nSrcDataLen    = (unsigned int)pFrameInfo->nFrameLen;
    cvtParam.enSrcPixelType = pFrameInfo->enPixelType;
    cvtParam.enDstPixelType = PixelType_Gvsp_BGR8_Packed;
    cvtParam.pDstBuffer     = frame->image->data;
    cvtParam.nDstBufferSize = (unsigned int)(frame->width * frame->height * 3);

    MV_CC_ConvertPixelType(hikvision_map[callback_param->camera->camera_id], &cvtParam);

    if (callback_param->flip) {
        cv::flip(*frame->image, *frame->image, 0);
    }

    callback_param->camera->buffer->push(frame);
}

bool rm::getHikVisionCameraNum(int& num)
{
    hikInitializeOptional();

    MV_CC_DEVICE_INFO_LIST dev_list;
    memset(&dev_list, 0, sizeof(MV_CC_DEVICE_INFO_LIST));
    MV_CHECK_API_ERROR(MV_CC_EnumDevices(MV_USB_DEVICE, &dev_list), "Failed to enumerate devices");

    num = dev_list.nDeviceNum;

    return true;
}

bool rm::setHikVisionArgs(Camera* camera, double exposure, double gain, double fps)
{
    void* handle = hikvision_map[camera->camera_id];
    MV_CHECK_API_WARNING(MV_CC_SetEnumValue(handle, "ExposureAuto", 0), "Failed to set manual exposure");
    MV_CHECK_API_WARNING(MV_CC_SetEnumValue(handle, "GainAuto", 0), "Failed to disable auto gain");
    MV_CHECK_API_WARNING(MV_CC_SetFloatValue(handle, "ExposureTime", (float)exposure), "Failed to set exposure time");
    MV_CHECK_API_WARNING(MV_CC_SetFloatValue(handle, "Gain", (float)gain), "Failed to set gain");
    MV_CHECK_API_WARNING(MV_CC_SetBoolValue(handle, "AcquisitionFrameRateEnable", true), "Failed to enable acquisition frame rate");
    MV_CHECK_API_WARNING(MV_CC_SetFloatValue(handle, "AcquisitionFrameRate", (float)fps), "Failed to set acquisition frame rate");

    return true;
}

bool rm::openHikVision(Camera* camera, int device_num, float* yaw_ptr,
    float* pitch_ptr, float* roll_ptr, bool flip, double exposure,
    double gain, double fps)
{
    MV_CC_DEVICE_INFO_LIST dev_list;
    memset(&dev_list, 0, sizeof(MV_CC_DEVICE_INFO_LIST));
    MV_CHECK_API_ERROR(MV_CC_EnumDevices(MV_USB_DEVICE, &dev_list), "Failed to enumerate devices");
    // device_num starts from 1 (same convention as MindVision)
    MV_ASSERT_ERROR(device_num >= 1 && device_num <= (int)dev_list.nDeviceNum, "Invalid HikVision device number");

    void* handle = nullptr;
    MV_CHECK_API_ERROR(MV_CC_CreateHandle(&handle, dev_list.pDeviceInfo[device_num - 1]), "Failed to create handle");
    MV_CHECK_API_ERROR(MV_CC_OpenDevice(handle, MV_ACCESS_Exclusive, 0), "Failed to open device");
    MV_CHECK_API_ERROR(MV_CC_SetEnumValue(handle, "TriggerMode", 0), "Failed to set trigger mode");
    hikvision_map[device_num] = handle;

    // camera_id and buffer must be ready before the callback fires
    camera->camera_id = device_num;
    if (camera->buffer != nullptr) delete camera->buffer;
    camera->buffer = new SwapBuffer<Frame>();

    MVCC_INTVALUE_EX widthVal, heightVal;
    MV_CHECK_API_WARNING(MV_CC_GetIntValueEx(handle, "Width", &widthVal), "Failed to get width value");
    MV_CHECK_API_WARNING(MV_CC_GetIntValueEx(handle, "Height", &heightVal), "Failed to get height value");
    camera->width = (int)widthVal.nCurValue;
    camera->height = (int)heightVal.nCurValue;

    setHikVisionArgs(camera, exposure, gain, fps);

    CallbackParam* callback_param = new CallbackParam;
    callback_param->camera = camera;
    callback_param->yaw = yaw_ptr;
    callback_param->pitch = pitch_ptr;
    callback_param->roll = roll_ptr;
    callback_param->flip = flip;
    MV_CHECK_API_ERROR(MV_CC_RegisterImageCallBackEx(handle, onFrameCallback, callback_param), "Failed to register image callback");

    MV_CHECK_API_ERROR(MV_CC_StartGrabbing(handle), "Failed to start grabbing");

    rm::message("Hikvision opened: " + std::to_string(camera->width) + "x" +
                std::to_string(camera->height), rm::MSG_OK);
    return true;
}

bool rm::closeHikVision()
{
    for (auto& kv : hikvision_map)
    {
        void* handle = kv.second;
        MV_CHECK_API_WARNING(MV_CC_StopGrabbing(handle), "Failed to stop grabbing");
        MV_CHECK_API_WARNING(MV_CC_CloseDevice(handle), "Failed to close device");
        MV_CHECK_API_WARNING(MV_CC_DestroyHandle(handle), "Failed to destroy handle");
    }
    hikvision_map.clear();
    hikFinalizeOptional();
    return true;
}
