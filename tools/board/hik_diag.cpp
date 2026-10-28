// Diagnose black frames: try several exposures, report raw bayer stats
#include "MvCameraControl.h"
#include <cstdio>
#include <cstring>
#include <unistd.h>

static void stats(const char* tag, const unsigned char* p, unsigned len) {
    unsigned mn = 255, mx = 0; unsigned long long sum = 0;
    for (unsigned i = 0; i < len; ++i) {
        unsigned v = p[i];
        if (v < mn) mn = v;
        if (v > mx) mx = v;
        sum += v;
    }
    printf("  %s: min=%u max=%u mean=%.2f (len=%u)\n", tag, mn, mx, (double)sum / len, len);
}

int main() {
    MV_CC_DEVICE_INFO_LIST list; memset(&list, 0, sizeof(list));
    if (MV_CC_EnumDevices(MV_USB_DEVICE, &list) != MV_OK || list.nDeviceNum == 0) {
        printf("no device\n"); return 1;
    }
    void* h = nullptr;
    MV_CC_CreateHandle(&h, list.pDeviceInfo[0]);
    if (MV_CC_OpenDevice(h, MV_ACCESS_Exclusive, 0) != MV_OK) { printf("open fail\n"); return 1; }

    MVCC_INTVALUE_EX w, hh;
    MV_CC_GetIntValueEx(h, "Width", &w);
    MV_CC_GetIntValueEx(h, "Height", &hh);
    printf("res %lldx%lld\n", (long long)w.nCurValue, (long long)hh.nCurValue);

    MV_CC_SetEnumValue(h, "TriggerMode", 0);
    MV_CC_SetEnumValue(h, "ExposureAuto", 0);
    MV_CC_SetEnumValue(h, "GainAuto", 0);

    MVCC_FLOATVALUE g;
    MV_CC_GetFloatValue(h, "Gain", &g);
    printf("gain range [%.1f, %.1f]\n", g.fMin, g.fMax);

    MV_CC_StartGrabbing(h);

    double exposures[] = {5000.0, 20000.0, 100000.0};
    for (int k = 0; k < 3; ++k) {
        MV_CC_SetFloatValue(h, "ExposureTime", (float)exposures[k]);
        MV_CC_SetFloatValue(h, "Gain", 16.0f);
        usleep(300000);
        for (int d = 0; d < 3; ++d) {   // flush stale frames
            MV_FRAME_OUT f; memset(&f, 0, sizeof(f));
            if (MV_CC_GetImageBuffer(h, &f, 1000) == MV_OK) MV_CC_FreeImageBuffer(h, &f);
        }
        MV_FRAME_OUT f; memset(&f, 0, sizeof(f));
        int rc = MV_CC_GetImageBuffer(h, &f, 2000);
        if (rc != MV_OK) { printf("exposure %.0f: get fail 0x%08X\n", exposures[k], rc); continue; }
        printf("exposure %.0f us, gain 16, pf=0x%08X len=%u:\n",
               exposures[k], (unsigned)f.stFrameInfo.enPixelType, f.stFrameInfo.nFrameLen);
        stats("raw", (const unsigned char*)f.pBufAddr,
              f.stFrameInfo.nFrameLen < 4000000 ? f.stFrameInfo.nFrameLen : 4000000);
        MV_CC_FreeImageBuffer(h, &f);
    }

    MV_CC_StopGrabbing(h);
    MV_CC_CloseDevice(h);
    MV_CC_DestroyHandle(h);
    return 0;
}