#include "video/video.h"
#include "uniterm/uniterm.h"
#include <algorithm>
#include <cctype>
#include <iostream>
#include <vector>
#include <iostream>
#include <fstream>
#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/videodev2.h>
#include <sys/mman.h>
#include <opencv2/opencv.hpp>
#include <cstdint>
#include <thread>
#include <memory>

static void capture_thread(rm::Camera* camera, rm::Locate* locate_ptr, int fps) {
    const int delay = (fps > 0) ? static_cast<int>(1000.0 / static_cast<double>(fps)) : 33;
    TimePoint last_time = getTime();
    while (camera->capture_run.load()) {
        int sleep_time = delay - static_cast<int>(getNumOfMs(last_time, getTime()));
        if (sleep_time > 0) std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time));
        last_time = getTime();

        struct v4l2_buffer buffer = {0};
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(camera->file_descriptor, VIDIOC_DQBUF, &buffer) < 0) {
            if (!camera->capture_run.load()) break;   // STREAMOFF 之后 DQBUF 会失败
            continue;
        }
        if (!camera->capture_run.load()) break;
        if (buffer.index >= camera->capture_buffer_num) continue;

        TimePoint time_stamp = getTime();
        rm::Locate locate{};
        if (locate_ptr != nullptr) {
            locate = *(locate_ptr);
        }

        // 注意：必须先把 mmap 缓冲区解码/拷贝出来，再重新入队（QBUF），
        // 否则内核可能在我们读取之前就覆盖了这块内存。
        cv::Mat image_bgr;
        bool decode_ok = true;

        if (camera->capture_format == 1) {
            // MJPEG：直接在压缩流上解码，省下 USB 带宽，1280x1024 也能跑高帧率
            cv::Mat jpeg(1, static_cast<int>(buffer.bytesused), CV_8UC1, camera->capture_buffer[buffer.index]);
            try {
                image_bgr = cv::imdecode(jpeg, cv::IMREAD_COLOR);
            } catch (const cv::Exception& e) {
                rm::message(std::string("Video UVC: mjpeg decode error ") + e.what(), rm::MSG_WARNING);
                decode_ok = false;
            }
            if (image_bgr.empty()) decode_ok = false;
        } else {
            cv::Mat image_yuv = cv::Mat(camera->height, camera->width, CV_8UC2, camera->capture_buffer[buffer.index]);
            try {
                cv::cvtColor(image_yuv, image_bgr, cv::COLOR_YUV2BGR_YUYV);
            } catch (const cv::Exception& e) {
                rm::message(std::string("Video UVC: cvt error at ") + e.what(), rm::MSG_ERROR);
                decode_ok = false;
            }
        }

        if (ioctl(camera->file_descriptor, VIDIOC_QBUF, &buffer) < 0) {
            rm::message("Video UVC: queue buffer error", rm::MSG_ERROR);
            continue;
        }

        if (!decode_ok) {
            // 丢帧，避免把空图推入流水线
            continue;
        }

        std::shared_ptr<rm::Frame> frame = std::make_shared<rm::Frame>();
        frame->time_point = time_stamp;
        frame->camera_id = camera->camera_id;
        frame->width = image_bgr.cols;
        frame->height = image_bgr.rows;
        frame->yaw = 0.0f;
        frame->pitch = 0.0f;
        frame->roll = 0.0f;
        frame->locate = locate;
        frame->image = std::make_shared<cv::Mat>(image_bgr);

        camera->buffer->push(frame);
    }
}

bool rm::testUVC(std::string& device_name) {
    const char* name = device_name.c_str();
    int fd = open(name, O_RDWR);
    if (fd == -1) {
        rm::message("Warning at opening camera: " + device_name, rm::MSG_WARNING);
        close(fd);
        return false;
    }

    struct v4l2_capability cap;
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
        rm::message("Warning at querying camera: " + device_name, rm::MSG_WARNING);
        close(fd);
        return false;
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        rm::message("The device does not handle video capture: " + device_name, rm::MSG_WARNING);
        close(fd);
        return false;
    }

    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        rm::message("The device does not handle streaming i/o: " + device_name, rm::MSG_WARNING);
        close(fd);
        return false;
    }

    struct v4l2_fmtdesc fmtdesc;
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmtdesc.index = 0;
    if (ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) < 0) {
        rm::message("Warning at querying camera format: " + device_name, rm::MSG_WARNING);
        close(fd);
        return false;
    }

    close(fd);
    return true;
}

bool rm::listUVC(std::vector<std::string>& device_list, std::string base_name) {
    device_list.clear();
    const std::string devPath = "/dev/";
    const std::string videoPrefix = base_name;

    DIR* dir = opendir(devPath.c_str());
    if (dir == nullptr) {
        rm::message("Failed to open directory: " + devPath, rm::MSG_ERROR);
        return false;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string fileName = entry->d_name;
        if (fileName.substr(0, videoPrefix.size()) == videoPrefix) {
            std::string deviceName = devPath + fileName;
            if (rm::testUVC(deviceName)) {
                device_list.push_back(deviceName);
            }
        }
    }

    closedir(dir);
    rm::message("Found " + std::to_string(device_list.size()) + " UVC devices.");
    return true;
}


bool rm::openUVCExt(
    Camera *camera,
    unsigned int width,
    unsigned int height,
    unsigned int fps,
    unsigned int buffer_num,
    const std::string& device_name,
    const std::string& pixel_format
) {
    if (camera == nullptr) {
        rm::message("Video UVC error at nullptr camera", rm::MSG_ERROR);
        return false;
    }

    // 解析像素格式
    unsigned int v4l2_format_id = V4L2_PIX_FMT_YUYV;
    int capture_format = 0;
    if (pixel_format == "MJPEG" || pixel_format == "mjpeg" || pixel_format == "MJPG") {
        v4l2_format_id = V4L2_PIX_FMT_MJPEG;
        capture_format = 1;
    } else if (pixel_format == "YUYV" || pixel_format == "yuyv") {
        v4l2_format_id = V4L2_PIX_FMT_YUYV;
        capture_format = 0;
    } else if (pixel_format == "NV12") {
        // 目前只实现了 YUYV 与 MJPEG 的转换，NV12 会得到错误颜色，直接拒绝
        rm::message("Video UVC does not support NV12 yet, use MJPEG or YUYV", rm::MSG_ERROR);
        return false;
    } else if (!pixel_format.empty()) {
        rm::message("Video UVC unsupported pixel format: " + pixel_format, rm::MSG_WARNING);
    }

    // 打开视频设备文件
    const char* chname = device_name.c_str();
    int fd = open(chname, O_RDWR);
    if (fd == -1) {
        rm::message("Video UVC error opening: " + device_name, rm::MSG_ERROR);
        return false;
    }

    // 设置摄像头的参数
    struct v4l2_format format = {0};
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.width = width;
    format.fmt.pix.height = height;
    format.fmt.pix.pixelformat = v4l2_format_id;
    format.fmt.pix.field = V4L2_FIELD_NONE;
    if (ioctl(fd, VIDIOC_S_FMT, &format) < 0) {
        rm::message("Video UVC error setting format: " + device_name, rm::MSG_ERROR);
        close(fd);
        return false;
    }

    // 驱动可能回退到别的分辨率/格式，以驱动返回值为准
    if (format.fmt.pix.pixelformat != v4l2_format_id) {
        rm::message("Video UVC: driver rejected pixel format, using what it returned", rm::MSG_WARNING);
    }
    const unsigned int real_width = format.fmt.pix.width;
    const unsigned int real_height = format.fmt.pix.height;
    rm::message("Video UVC size: " + std::to_string(real_width) + "x" + std::to_string(real_height));

    // 设置帧率
    struct v4l2_streamparm streamparam = {0};
    streamparam.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    streamparam.parm.capture.timeperframe.numerator = 1;
    streamparam.parm.capture.timeperframe.denominator = (fps > 0) ? fps : 30;
    if (ioctl(fd, VIDIOC_S_PARM, &streamparam) < 0) {
        rm::message("Video UVC warning setting frame rate (ignored): " + device_name, rm::MSG_WARNING);
    }
    if (streamparam.parm.capture.timeperframe.denominator > 0 &&
        streamparam.parm.capture.timeperframe.numerator > 0) {
        const double real_fps =
            static_cast<double>(streamparam.parm.capture.timeperframe.denominator) /
            static_cast<double>(streamparam.parm.capture.timeperframe.numerator);
        rm::message("Video UVC fps: " + std::to_string(real_fps));
    }

    // 申请内核空间
    camera->capture_buffer_num = std::clamp(buffer_num, 2u, 128u);
    struct v4l2_requestbuffers requestbuffers = {0};
    requestbuffers.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    requestbuffers.count = camera->capture_buffer_num;
    requestbuffers.memory = V4L2_MEMORY_MMAP;
    if (ioctl(fd, VIDIOC_REQBUFS, &requestbuffers) < 0) {
        rm::message("Video UVC error requesting buffer: " + device_name, rm::MSG_ERROR);
        close(fd);
        return false;
    }

    // 驱动可能给的比要求的少，必须以驱动返回的 count 为准
    if (requestbuffers.count < 2) {
        rm::message("Video UVC: driver gave only " + std::to_string(requestbuffers.count) +
                    " buffer(s), at least 2 needed", rm::MSG_ERROR);
        close(fd);
        return false;
    }
    camera->capture_buffer_num = requestbuffers.count;

    // 映射内核空间到用户空间
    camera->capture_buffer = new uint8_t*[camera->capture_buffer_num];
    camera->capture_buffer_size = new uint32_t[camera->capture_buffer_num];
    struct v4l2_buffer mapbuffer = {0};
    mapbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    for(uint32_t i = 0; i < camera->capture_buffer_num; i++) {
        mapbuffer.index = i;
        if (ioctl(fd, VIDIOC_QUERYBUF, &mapbuffer) < 0) {
            rm::message("Video UVC error querying buffer: " + device_name, rm::MSG_ERROR);
            delete[] camera->capture_buffer;
            delete[] camera->capture_buffer_size;
            close(fd);
            return false;
        }

        camera->capture_buffer[i] = (uint8_t*)mmap(NULL, mapbuffer.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, mapbuffer.m.offset);
        camera->capture_buffer_size[i] = mapbuffer.length;

        if (ioctl(fd, VIDIOC_QBUF, &mapbuffer) < 0) {
            rm::message("Video UVC error mmap buffer: " + device_name, rm::MSG_ERROR);
            for (uint32_t j = 0; j < i; j++) {
                munmap(camera->capture_buffer[j], camera->capture_buffer_size[j]);
            }
            delete[] camera->capture_buffer;
            delete[] camera->capture_buffer_size;
            close(fd);
            return false;
        }
    }

    // 设置Camera参数
    camera->file_descriptor = fd;
    camera->width = static_cast<int>(real_width);
    camera->height = static_cast<int>(real_height);
    camera->capture_format = capture_format;
    camera->uvc_capture = true;
    if (camera->buffer != nullptr) {
        delete camera->buffer;
    }
    camera->buffer = new rm::SwapBuffer<rm::Frame>();
    
    int temp_id = 0;
    int start_index = static_cast<int>(device_name.length()) - 1;
    while(start_index >= 0 && std::isdigit(static_cast<unsigned char>(device_name[start_index]))) {
        temp_id = temp_id * 10 + (device_name[start_index] - '0');
        --start_index;
    }
    
    camera->camera_id = 0;
    while (temp_id > 0) {
        camera->camera_id = camera->camera_id * 10 + temp_id % 10;
        temp_id /= 10;
    }

    rm::message("Video UVC opened: " + device_name, rm::MSG_OK);

    return true;
}

bool rm::openUVC(
    Camera *camera,
    unsigned int width,
    unsigned int height,
    unsigned int fps,
    unsigned int buffer_num,
    std::string device_name
) {
    return rm::openUVCExt(camera, width, height, fps, buffer_num, device_name, "YUYV");
}

bool rm::setUVC(
    Camera *camera,
    int exposure,
    int brightness,
    int contrast,
    int gamma,
    int gain,
    int sharpness,
    int backlight
) {
    struct v4l2_control ctrl; 
    ctrl.id = V4L2_CID_EXPOSURE_AUTO;
    ctrl.value = V4L2_EXPOSURE_MANUAL;
    if (ioctl(camera->file_descriptor, VIDIOC_S_CTRL, &ctrl) < 0) {
        rm::message("Video UVC error setting exposure mode", rm::MSG_ERROR);
        return false;
    }

    ctrl.id = V4L2_CID_EXPOSURE_ABSOLUTE;
    ctrl.value = exposure;
    if (ioctl(camera->file_descriptor, VIDIOC_S_CTRL, &ctrl) < 0) {
        rm::message("Video UVC error setting exposure value", rm::MSG_ERROR);
        return false;
    }

    ctrl.id = V4L2_CID_BRIGHTNESS;
    ctrl.value = brightness;
    if (ioctl(camera->file_descriptor, VIDIOC_S_CTRL, &ctrl) < 0) {
        rm::message("Video UVC error setting brightness", rm::MSG_ERROR);
        return false;
    }

    ctrl.id = V4L2_CID_CONTRAST;
    ctrl.value = contrast;
    if (ioctl(camera->file_descriptor, VIDIOC_S_CTRL, &ctrl) < 0) {
        rm::message("Video UVC error setting contrast", rm::MSG_ERROR);
        return false;
    }

    ctrl.id = V4L2_CID_GAMMA;
    ctrl.value = gamma;
    if (ioctl(camera->file_descriptor, VIDIOC_S_CTRL, &ctrl) < 0) {
        rm::message("Video UVC error setting gamma", rm::MSG_ERROR);
        return false;
    }

    ctrl.id = V4L2_CID_GAIN;
    ctrl.value = gain;
    if (ioctl(camera->file_descriptor, VIDIOC_S_CTRL, &ctrl) < 0) {
        rm::message("Video UVC error setting gain", rm::MSG_ERROR);
        return false;
    }

    ctrl.id = V4L2_CID_SHARPNESS;
    ctrl.value = sharpness;
    if (ioctl(camera->file_descriptor, VIDIOC_S_CTRL, &ctrl) < 0) {
        rm::message("Video UVC error setting sharpness", rm::MSG_ERROR);
        return false;
    }

    ctrl.id = V4L2_CID_BACKLIGHT_COMPENSATION;
    ctrl.value = backlight;
    if (ioctl(camera->file_descriptor, VIDIOC_S_CTRL, &ctrl) < 0) {
        rm::message("Video UVC error setting backlight", rm::MSG_ERROR);
        return false;
    }

    ctrl.id = V4L2_CID_AUTO_WHITE_BALANCE;
    ctrl.value = 1;
    if (ioctl(camera->file_descriptor, VIDIOC_S_CTRL, &ctrl) < 0) {
        rm::message("Video UVC error setting white balance mode", rm::MSG_ERROR);
        return false;
    }

    return true;
}

bool rm::runUVC(Camera *camera, Locate* locate_ptr, int fps) {
    if (camera == nullptr) {
        rm::message("Video UVC error at nullptr camera", rm::MSG_ERROR);
        return false;
    }

    // 开始采集
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(camera->file_descriptor, VIDIOC_STREAMON, &type) < 0) {
        rm::message("Video UVC error starting stream", rm::MSG_ERROR);
        return false;
    }

    // 启动采集线程（不 detach：closeUVC 里要 join，否则会 munmap 到正在用的内存）
    if (camera->capture_thread.joinable()) camera->capture_thread.join();
    camera->capture_run = true;
    camera->capture_thread = std::thread(&capture_thread, camera, locate_ptr, fps);
    rm::message("Video UVC start capture: " + std::to_string(camera->camera_id), rm::MSG_OK);
    return true;
}

bool rm::closeUVC(Camera *camera) {
    if (camera == nullptr) {
        rm::message("Video UVC error at nullptr camera", rm::MSG_ERROR);
        return false;
    }

    // 先让采集线程退出，再做 STREAMOFF / munmap
    camera->capture_run = false;

    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(camera->file_descriptor, VIDIOC_STREAMOFF, &type) < 0) {
        rm::message("Video UVC warning stopping stream", rm::MSG_WARNING);
    }

    if (camera->capture_thread.joinable()) camera->capture_thread.join();

    // 释放内核空间
    for (uint32_t i = 0; i < camera->capture_buffer_num; i++) {
        if (camera->capture_buffer != nullptr && camera->capture_buffer[i] != nullptr) {
            munmap(camera->capture_buffer[i], camera->capture_buffer_size[i]);
        }
    }
    delete[] camera->capture_buffer;
    delete[] camera->capture_buffer_size;
    camera->capture_buffer = nullptr;
    camera->capture_buffer_size = nullptr;
    camera->capture_buffer_num = 0;

    // 关闭设备
    close(camera->file_descriptor);
    delete camera->buffer;
    camera->buffer = nullptr;
    camera->uvc_capture = false;

    rm::message("Video UVC closed: " + std::to_string(camera->camera_id), rm::MSG_WARNING);
    return true;
}