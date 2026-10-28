// Hikrobot MV-CS016-10UC direct test (MVS 3.x API)
#include "MvCameraControl.h"

#include <cstdio>
#include <cstring>
#include <unistd.h>

int main() {
    MV_CC_DEVICE_INFO_LIST list;
    memset(&list, 0, sizeof(list));
    int rc = MV_CC_EnumDevices(MV_USB_DEVICE, &list);
    printf("EnumDevices rc=0x%08X n=%u\n", rc, list.nDeviceNum);
    if (list.nDeviceNum == 0) { printf("NO DEVICE\n"); return 1; }
    for (unsigned i = 0; i < list.nDeviceNum; ++i) {
        MV_CC_DEVICE_INFO* info = list.pDeviceInfo[i];
        if (info && info->nTLayerType == MV_USB_DEVICE) {
            printf("dev[%u] model=%s serial=%s\n", i,
                   (char*)info->SpecialInfo.stUsb3VInfo.chModelName,
                   (char*)info->SpecialInfo.stUsb3VInfo.chSerialNumber);
        }
    }

    void* handle = nullptr;
    rc = MV_CC_CreateHandle(&handle, list.pDeviceInfo[0]);
    printf("CreateHandle rc=0x%08X\n", rc);
    if (rc != MV_OK) return 1;
    rc = MV_CC_OpenDevice(handle, MV_ACCESS_Exclusive, 0);
    printf("OpenDevice rc=0x%08X\n", rc);
    if (rc != MV_OK) return 1;

    MVCC_INTVALUE_EX w, h;
    MV_CC_GetIntValueEx(handle, "Width", &w);
    MV_CC_GetIntValueEx(handle, "Height", &h);
    printf("resolution %lldx%lld\n", (long long)w.nCurValue, (long long)h.nCurValue);

    MVCC_ENUMVALUE pf; memset(&pf, 0, sizeof(pf));
    MV_CC_GetEnumValue(handle, "PixelFormat", &pf);
    printf("pixelformat 0x%08X\n", pf.nCurValue);

    MV_CC_SetEnumValue(handle, "TriggerMode", 0);
    MV_CC_SetEnumValue(handle, "ExposureAuto", 0);
    MV_CC_SetEnumValue(handle, "GainAuto", 0);
    MV_CC_SetFloatValue(handle, "ExposureTime", 5000.0f);
    MV_CC_SetFloatValue(handle, "Gain", 0.0f);
    MV_CC_SetBoolValue(handle, "AcquisitionFrameRateEnable", true);
    MV_CC_SetFloatValue(handle, "AcquisitionFrameRate", 60.0f);
    MVCC_FLOATVALUE fe, fg, ff;
    MV_CC_GetFloatValue(handle, "ExposureTime", &fe);
    MV_CC_GetFloatValue(handle, "Gain", &fg);
    MV_CC_GetFloatValue(handle, "AcquisitionFrameRate", &ff);
    printf("exposure=%.1f [%.1f,%.1f]  gain=%.1f  fps=%.1f\n",
           fe.fCurValue, fe.fMin, fe.fMax, fg.fCurValue, ff.fCurValue);

    rc = MV_CC_StartGrabbing(handle);
    printf("StartGrabbing rc=0x%08X\n", rc);
    if (rc != MV_OK) return 1;

    const int W = (int)w.nCurValue, H = (int)h.nCurValue;
    unsigned char* bgr = new unsigned char[(size_t)W * H * 3];
    int saved = 0;
    for (int i = 0; i < 10 && saved < 2; ++i) {
        MV_FRAME_OUT frame; memset(&frame, 0, sizeof(frame));
        rc = MV_CC_GetImageBuffer(handle, &frame, 2000);
        if (rc != MV_OK) { printf("GetImageBuffer rc=0x%08X\n", rc); continue; }

        MV_CC_PIXEL_CONVERT_PARAM cvt; memset(&cvt, 0, sizeof(cvt));
        cvt.nWidth = (unsigned short)frame.stFrameInfo.nWidth;
        cvt.nHeight = (unsigned short)frame.stFrameInfo.nHeight;
        cvt.pSrcData = frame.pBufAddr;
        cvt.nSrcDataLen = (unsigned int)frame.stFrameInfo.nFrameLen;
        cvt.enSrcPixelType = frame.stFrameInfo.enPixelType;
        cvt.enDstPixelType = PixelType_Gvsp_BGR8_Packed;
        cvt.pDstBuffer = bgr;
        cvt.nDstBufferSize = (unsigned int)(W * H * 3);
        rc = MV_CC_ConvertPixelType(handle, &cvt);
        if (rc != MV_OK) {
            printf("Convert rc=0x%08X src_pf=0x%08X len=%u\n",
                   rc, (unsigned)frame.stFrameInfo.enPixelType, frame.stFrameInfo.nFrameLen);
            MV_CC_FreeImageBuffer(handle, &frame);
            continue;
        }

        char path[64];
        snprintf(path, sizeof(path), "/root/smoke/hik_%d.ppm", saved);
        FILE* f = fopen(path, "wb");
        if (f) {
            fprintf(f, "P6\n%d %d\n255\n", cvt.nWidth, cvt.nHeight);
            for (int p = 0; p < cvt.nWidth * cvt.nHeight; ++p) {
                unsigned char px[3] = {bgr[p * 3 + 2], bgr[p * 3 + 1], bgr[p * 3]};
                fwrite(px, 1, 3, f);
            }
            fclose(f);
            printf("saved %s (%dx%d) src_pf=0x%08X\n", path, cvt.nWidth, cvt.nHeight,
                   (unsigned)frame.stFrameInfo.enPixelType);
            ++saved;
        }
        MV_CC_FreeImageBuffer(handle, &frame);
    }

    MV_CC_StopGrabbing(handle);
    MV_CC_CloseDevice(handle);
    MV_CC_DestroyHandle(handle);
    printf("done\n");
    return 0;
}