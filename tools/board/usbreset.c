// 直接对 /dev/bus/usb/BBB/DDD 做 USBDEVFS_RESET（不依赖 libusb 头文件）
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/usbdevice_fs.h>
#include <errno.h>

int main(int argc, char** argv) {
    const char* dev = (argc > 1) ? argv[1] : "/dev/bus/usb/002/002";
    int fd = open(dev, O_WRONLY);
    if (fd < 0) { printf("open %s failed: %s\n", dev, strerror(errno)); return 1; }
    int rc = ioctl(fd, USBDEVFS_RESET, 0);
    printf("USBDEVFS_RESET(%s) rc=%d (%s)\n", dev, rc, rc < 0 ? strerror(errno) : "ok");
    close(fd);
    return rc < 0 ? 1 : 0;
}
