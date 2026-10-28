// 鐢?libusb 鐩存帴鎺㈡祴鐩告満锛堟墜鍔ㄥ０鏄?API锛岄伩鍏嶄緷璧?dev 澶存枃浠讹級
#include <cstdio>
#include <cstdint>

extern "C" {
struct libusb_context;
struct libusb_device;
struct libusb_device_handle;
int libusb_init(libusb_context** ctx);
void libusb_exit(libusb_context* ctx);
ssize_t libusb_get_device_list(libusb_context* ctx, libusb_device*** list);
void libusb_free_device_list(libusb_device** list, int unref_devices);
int libusb_get_device_descriptor(libusb_device* dev, struct libusb_device_descriptor* desc);
int libusb_open(libusb_device* dev, libusb_device_handle** handle);
void libusb_close(libusb_device_handle* h);
int libusb_claim_interface(libusb_device_handle* h, int iface);
int libusb_release_interface(libusb_device_handle* h, int iface);
int libusb_control_transfer(libusb_device_handle* h, uint8_t bmRequestType, uint8_t bRequest,
                            uint16_t wValue, uint16_t wIndex, unsigned char* data,
                            uint16_t wLength, unsigned int timeout);
const char* libusb_error_name(int code);
}

struct libusb_device_descriptor {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t bcdUSB;
    uint8_t bDeviceClass, bDeviceSubClass, bDeviceProtocol, bMaxPacketSize0;
    uint16_t idVendor, idProduct, bcdDevice;
    uint8_t iManufacturer, iProduct, iSerialNumber, bNumConfigurations;
};

int main() {
    libusb_context* ctx = nullptr;
    int rc = libusb_init(&ctx);
    printf("libusb_init rc=%d (%s)\n", rc, libusb_error_name(rc));
    if (rc != 0) return 1;

    libusb_device** list = nullptr;
    ssize_t n = libusb_get_device_list(ctx, &list);
    printf("devices: %zd\n", n);

    libusb_device_handle* h = nullptr;
    for (ssize_t i = 0; i < n; ++i) {
        libusb_device_descriptor d{};
        if (libusb_get_device_descriptor(list[i], &d) != 0) continue;
        if (d.idVendor != 0x2bdf) continue;
        printf("found 2bdf:%04x bcdUSB=%04x class=%02x/%02x/%02x\n",
               d.idProduct, d.bcdUSB, d.bDeviceClass, d.bDeviceSubClass, d.bDeviceProtocol);
        rc = libusb_open(list[i], &h);
        printf("libusb_open rc=%d (%s)\n", rc, libusb_error_name(rc));
        break;
    }
    if (h == nullptr) { printf("open failed\n"); return 1; }

    rc = libusb_claim_interface(h, 0);
    printf("claim_interface(0) rc=%d (%s)\n", rc, libusb_error_name(rc));

    // U3V: 璇?ABRM 鐨?U3V Version (瀵勫瓨鍣?0x0000, 4 瀛楄妭)
    unsigned char buf[8] = {0};
    rc = libusb_control_transfer(h, 0x80, 0x84, 0x0000, 0x0000, buf, 4, 1000);
    printf("read U3V version rc=%d (%s) bytes=%02x %02x %02x %02x\n",
           rc, libusb_error_name(rc), buf[0], buf[1], buf[2], buf[3]);

    // 璇?ManufacturerName (瀵勫瓨鍣?0x0004, 64 瀛楄妭)
    unsigned char name[64] = {0};
    rc = libusb_control_transfer(h, 0x80, 0x84, 0x0000, 0x0004, name, 64, 1000);
    printf("read manufacturer rc=%d name='%.*s'\n", rc, rc > 0 ? rc : 0, name);

    // 璇?ModelName (0x0044)
    unsigned char model[64] = {0};
    rc = libusb_control_transfer(h, 0x80, 0x84, 0x0000, 0x0044, model, 64, 1000);
    printf("read model rc=%d model='%.*s'\n", rc, rc > 0 ? rc : 0, model);

    if (rc >= 0) libusb_release_interface(h, 0);
    libusb_close(h);
    libusb_free_device_list(list, 1);
    libusb_exit(ctx);
    printf("done\n");
    return 0;
}
