#include "MvCameraControl.h"
#include <cstdio>
#include <cstring>

int main() {
    unsigned int ver = MV_CC_GetSDKVersion();
    printf("SDK version = 0x%08X  (%u.%u.%u.%u)\n", ver,
           (ver >> 24) & 0xff, (ver >> 16) & 0xff, (ver >> 8) & 0xff, ver & 0xff);

    MV_CC_DEVICE_INFO_LIST l;
    memset(&l, 0, sizeof(l));
    int rc = MV_CC_EnumDevices(MV_USB_DEVICE, &l);
    printf("EnumDevices USB  rc=0x%08X n=%u\n", rc, l.nDeviceNum);

    memset(&l, 0, sizeof(l));
    rc = MV_CC_EnumDevices(MV_GIGE_DEVICE | MV_USB_DEVICE, &l);
    printf("EnumDevices BOTH rc=0x%08X n=%u\n", rc, l.nDeviceNum);
    return 0;
}