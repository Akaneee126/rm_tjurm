#!/bin/sh
cd /root/smoke
echo "=== LD_DEBUG: libs not found / init calls ==="
LD_LIBRARY_PATH=/usr/local/lib LD_DEBUG=libs ./hik_probe 2>&1 | grep -iE "not found|error|calling init" | head -20
echo
echo "=== with MVCAM env ==="
export MVCAM_COMMON_RUNENV=/usr/local/lib
export MVCAM_SDK_PATH=/usr/local
export MVCAM_GENICAM_CLPROTOCOL=/usr/local/lib
LD_LIBRARY_PATH=/usr/local/lib ./hik_probe 2>&1 | head -8
echo
echo "=== try MV_GIGE_DEVICE|MV_USB_DEVICE via EnumDevicesEx ==="
cat > /root/smoke/enum2.cpp <<'EOF'
#include "MvCameraControl.h"
#include <cstdio>
#include <cstring>
int main() {
    MV_CC_DEVICE_INFO_LIST l; memset(&l,0,sizeof(l));
    int rc = MV_CC_EnumDevices(MV_GIGE_DEVICE, &l);
    printf("GIGE   rc=0x%08X n=%u\n", rc, l.nDeviceNum);
    memset(&l,0,sizeof(l));
    rc = MV_CC_EnumDevices(MV_USB_DEVICE, &l);
    printf("USB    rc=0x%08X n=%u\n", rc, l.nDeviceNum);
    memset(&l,0,sizeof(l));
    rc = MV_CC_EnumDevices(MV_GIGE_DEVICE | MV_USB_DEVICE, &l);
    printf("BOTH   rc=0x%08X n=%u\n", rc, l.nDeviceNum);
    memset(&l,0,sizeof(l));
    rc = MV_CC_EnumDevicesEx2(&l, nullptr);
    printf("Ex2    rc=0x%08X n=%u\n", rc, l.nDeviceNum);
    return 0;
}
EOF
g++ -O2 -std=c++17 enum2.cpp -I/root/OpenRM-main/include/video/hikvision \
    -L/usr/local/lib -lMvCameraControl -o enum2
LD_LIBRARY_PATH=/usr/local/lib ./enum2 2>&1
