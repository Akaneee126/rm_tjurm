#include <string>
#include <vector>
#include <thread>
#include <iostream>
#include <fstream>
#include <chrono>

#include "data_manager/base.h"
#include "data_manager/param.h"
#include "data_manager/model.h"
#include "threads/pipeline.h"
#include "threads/control.h"
#include "garage/garage.h"

namespace {

std::string cfgString(const nlohmann::json& j, const char* key, const std::string& def) {
    if (j.contains(key) && j[key].is_string()) return j[key].get<std::string>();
    return def;
}

double cfgNumber(const nlohmann::json& j, const char* key, double def) {
    if (j.contains(key) && j[key].is_number()) return j[key].get<double>();
    return def;
}

bool cfgBool(const nlohmann::json& j, const char* key, bool def) {
    if (j.contains(key) && j[key].is_boolean()) return j[key].get<bool>();
    return def;
}

// 相机驱动类型
enum class CameraDriver {
    MINDVISION,
    DAHENG,
    HIKVISION,
    UVC,
    VIDEO          // 用视频文件回放当相机（台架调试/录像复跑用）
};

CameraDriver parseDriver(const std::string& name) {
    if (name == "UVC" || name == "uvc")             return CameraDriver::UVC;
    if (name == "DaHeng" || name == "daheng")       return CameraDriver::DAHENG;
    if (name == "HikVision" || name == "hikvision") return CameraDriver::HIKVISION;
    if (name == "Video" || name == "video")         return CameraDriver::VIDEO;
    return CameraDriver::MINDVISION;
}

// 视频文件"相机"：按帧率把帧推进 SwapBuffer，接口与真实相机一致
bool openVideoFile(rm::Camera* camera, const nlohmann::json& cfg, int index) {
    const std::string path = cfgString(cfg, "VideoPath", "");
    if (path.empty()) {
        rm::message("Camera.VideoPath is empty", rm::MSG_ERROR);
        return false;
    }

    auto cap = std::make_shared<cv::VideoCapture>(path);
    if (!cap->isOpened()) {
        rm::message("Cannot open video: " + path, rm::MSG_ERROR);
        return false;
    }

    camera->camera_id = index;
    camera->width  = static_cast<int>(cap->get(cv::CAP_PROP_FRAME_WIDTH));
    camera->height = static_cast<int>(cap->get(cv::CAP_PROP_FRAME_HEIGHT));
    camera->capture_format = 0;
    camera->uvc_capture = false;
    // 安装姿态补偿（1=上下翻 2=左右镜像 3=180°），在采帧线程里应用
    camera->flip_mode = static_cast<int>(cfgNumber(cfg, "FlipMode", cfgBool(cfg, "Rotate", false) ? 1.0 : 0.0));

    if (camera->buffer != nullptr) delete camera->buffer;
    camera->buffer = new rm::SwapBuffer<rm::Frame>();

    const double fps  = cfgNumber(cfg, "FrameRate", 30.0);
    const bool   loop = cfgBool(cfg, "Loop", true);
    const int    delay_ms = (fps > 0.0) ? static_cast<int>(1000.0 / fps) : 33;

    rm::message("Video replay: " + path + " " + std::to_string(camera->width) + "x" +
                std::to_string(camera->height) + " loop=" + (loop ? "1" : "0"), rm::MSG_NOTE);

    if (camera->capture_thread.joinable()) camera->capture_thread.join();
    camera->capture_run = true;
    camera->capture_thread = std::thread([cap, camera, delay_ms, loop]() {
        cv::Mat img;
        while (camera->capture_run.load()) {
            if (!cap->read(img) || img.empty()) {
                if (!loop) break;
                cap->set(cv::CAP_PROP_POS_FRAMES, 0);
                continue;
            }
            auto frame = std::make_shared<rm::Frame>();
            frame->time_point = getTime();
            frame->camera_id = camera->camera_id;
            frame->width  = img.cols;
            frame->height = img.rows;
            frame->yaw = frame->pitch = frame->roll = 0.0f;
            frame->locate = rm::Locate{};
            if (camera->flip_mode == 1)      cv::flip(img, img, 0);
            else if (camera->flip_mode == 2) cv::flip(img, img, 1);
            else if (camera->flip_mode == 3) cv::flip(img, img, -1);
            frame->image = std::make_shared<cv::Mat>(img);
            camera->buffer->push(frame);
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        }
    });
    return true;
}

// 打开一台相机（驱动分发）
bool openCameraByDriver(
    rm::Camera* camera,
    CameraDriver driver,
    int index,
    const nlohmann::json& cfg,
    float* yaw,
    float* pitch,
    float* roll
) {
    const double exposure = cfgNumber(cfg, "ExposureTime", 5000.0);
    const double gain     = cfgNumber(cfg, "Gain", 0.0);
    const double fps      = cfgNumber(cfg, "FrameRate", 60.0);
    const bool   rotate   = cfgBool(cfg, "Rotate", false);
    const bool   awb      = cfgBool(cfg, "AutoWhiteBalance", false);

    // 安装姿态补偿：1=上下翻(Y轴) 2=左右镜像(X轴) 3=180°；兼容旧的 Rotate
    camera->flip_mode = static_cast<int>(cfgNumber(cfg, "FlipMode", rotate ? 1.0 : 0.0));

    switch (driver) {
        case CameraDriver::VIDEO:
            return openVideoFile(camera, cfg, index);

        case CameraDriver::UVC: {
            const int width     = static_cast<int>(cfgNumber(cfg, "Width", 1280));
            const int height    = static_cast<int>(cfgNumber(cfg, "Height", 1024));
            const int buffer    = static_cast<int>(cfgNumber(cfg, "BufferNum", 8));
            const std::string device = cfgString(cfg, "Device", "/dev/video0");
            const std::string format = cfgString(cfg, "PixelFormat", "MJPEG");

            rm::message("UVC open " + device + " " + std::to_string(width) + "x" +
                        std::to_string(height) + "@" + std::to_string(fps) + " " + format,
                        rm::MSG_NOTE);

            if (!rm::openUVCExt(camera, width, height,
                                static_cast<unsigned int>(fps),
                                static_cast<unsigned int>(buffer),
                                device, format)) {
                return false;
            }

            // 曝光/增益等 V4L2 控件：不同摄像头支持程度不同，失败只告警
            if (cfgBool(cfg, "ApplyV4L2Controls", false)) {
                if (!rm::setUVC(camera,
                                static_cast<int>(exposure),
                                static_cast<int>(cfgNumber(cfg, "Brightness", 0)),
                                static_cast<int>(cfgNumber(cfg, "Contrast", 32)),
                                static_cast<int>(cfgNumber(cfg, "Gamma", 100)),
                                static_cast<int>(gain),
                                static_cast<int>(cfgNumber(cfg, "Sharpness", 16)),
                                static_cast<int>(cfgNumber(cfg, "Backlight", 1)))) {
                    rm::message("UVC controls partly unsupported, ignored", rm::MSG_WARNING);
                }
            }

            if (!rm::runUVC(camera, nullptr, static_cast<int>(fps > 0 ? fps : 60))) return false;
            return true;
        }

        case CameraDriver::DAHENG:
            return rm::openDaHeng(camera, index, yaw, pitch, roll, rotate, exposure, gain, fps);

        case CameraDriver::HIKVISION:
            return rm::openHikVision(camera, index, yaw, pitch, roll, rotate, exposure, gain, fps);

        case CameraDriver::MINDVISION:
        default:
            return rm::openMindVision(camera, index, yaw, pitch, roll, rotate, awb, exposure, gain, fps);
    }
}

// 相机 -> CamLens.json 中的标定条目
bool applyLensCalibration(
    rm::Camera* camera,
    const nlohmann::json& camlens,
    const std::string& camera_type,
    const std::string& lens_type,
    const std::vector<double>& offset
) {
    bool ok = true;

    if (camlens.contains(camera_type) && camlens[camera_type].contains(lens_type)) {
        Param::from_json(camlens[camera_type][lens_type]["Intrinsic"], camera->intrinsic_matrix);
        Param::from_json(camlens[camera_type][lens_type]["Distortion"], camera->distortion_coeffs);
        rm::message("Lens calibration loaded: " + camera_type + "/" + lens_type, rm::MSG_OK);
    } else {
        // 没有内参就无法 PnP，直接报错，避免后面给出一堆离谱的角度
        rm::message("Lens calibration NOT found for " + camera_type + "/" + lens_type +
                    ", please calibrate and add it into CamLens.json", rm::MSG_ERROR);
        ok = false;
    }

    const std::vector<double> off = (offset.size() >= 5)
        ? offset
        : std::vector<double>{0.0, 0.0, 0.0, 0.0, 0.0};

    rm::tf_rotate_pnp2head(camera->Rotate_pnp2head, off[3], off[4], 0.0);
    rm::tf_trans_pnp2head(camera->Trans_pnp2head, off[0], off[1], off[2], off[3], off[4], 0.0);

    return ok;
}

}  // namespace


void init_debug() {
    auto param = Param::get_instance();
    Data::auto_fire = (*param)["Debug"]["System"]["AutoFire"];
    Data::auto_enemy = (*param)["Debug"]["System"]["AutoEnemy"];
    Data::auto_rune = (*param)["Debug"]["System"]["AutoRune"];
    Data::auto_capture = (*param)["Debug"]["System"]["AutoCapture"];

    Data::plus_pnp = (*param)["Debug"]["PlusPnP"]["Enable"];

    Data::serial_flag = (*param)["Debug"]["Control"]["Serial"];
    Data::timeout_flag = (*param)["Debug"]["Control"]["Timeout"];
    Data::manu_capture = (*param)["Debug"]["Control"]["ManuCapture"];
    Data::manu_fire = (*param)["Debug"]["Control"]["ManuFire"];

    Data::manu_rune = (*param)["Debug"]["Control"]["ManuRune"];
    Data::big_rune = (*param)["Debug"]["Control"]["BigRune"];

    Data::ui_flag = (*param)["Debug"]["ImageThread"]["UI"];
    Data::imwrite_flag = (*param)["Debug"]["ImageThread"]["Imwrite"];
    Data::image_flag = (bool)(Data::imwrite_flag || Data::imshow_flag);
    Data::binary_flag = (*param)["Debug"]["ImageThread"]["Binary"];
    Data::histogram_flag = (*param)["Debug"]["ImageThread"]["Histogram"];

    Data::reprojection_flag = (*param)["Debug"]["Display"]["Reprojection"];
    Data::pipeline_delay_flag = (*param)["Debug"]["Display"]["PipelineDelay"];
    Data::point_skip_flag = (*param)["Debug"]["Display"]["PointSkip"];

    Data::state_delay_flag = (*param)["Debug"]["StateDelay"]["Enable"];
    Data::state_delay_time = (*param)["Debug"]["StateDelay"]["TimeS"];
    Data::state_queue_size = (*param)["Debug"]["StateDelay"]["QueueSize"];
    Data::send_wait_time = (*param)["Debug"]["StateDelay"]["SendWait"];
}

bool init_camera() {
    auto param = Param::get_instance();

    // 获取相机参数矩阵json
    nlohmann::json camlens;
    std::string camlen_path = (*param)["Camera"]["CamLensDir"];
    try {
        std::ifstream camlens_json(camlen_path);
        camlens_json >> camlens;
        camlens_json.close();
    } catch (std::exception& e) {
        std::string err_str = "Failed to load CamLens json: " + std::string(e.what());
        rm::message(err_str, rm::MSG_ERROR);
        return false;
    }

    nlohmann::json& camera_cfg = (*param)["Camera"];
    const CameraDriver driver = parseDriver(cfgString(camera_cfg, "Driver", "MindVision"));

    // 枚举相机
    int camera_num = 0;
    std::vector<std::string> uvc_devices;

    if (driver == CameraDriver::VIDEO) {
        camera_num = 1;
        rm::message("Camera driver: video replay", rm::MSG_NOTE);
    } else if (driver == CameraDriver::UVC) {
        const std::string base_name = cfgString(camera_cfg, "UVCName", "video");
        if (!rm::listUVC(uvc_devices, base_name) || uvc_devices.empty()) {
            rm::message("No UVC camera found under /dev/" + base_name + "*", rm::MSG_ERROR);
            return false;
        }
        camera_num = static_cast<int>(uvc_devices.size());
        rm::message("Found " + std::to_string(camera_num) + " UVC device(s)", rm::MSG_NOTE);

        // 很多 UVC 摄像头会额外暴露一个 capture 节点（例如 raw + MJPEG 各一个），
        // 默认只当单相机用，避免被误判成双目
        if (!cfgBool(camera_cfg, "DualUVC", false)) {
            camera_num = 1;
        }
    } else if (driver == CameraDriver::DAHENG) {
        bool flag_camera = rm::getDaHengCameraNum(camera_num);
        if (!flag_camera) {
            rm::message("Failed to get DaHeng camera number", rm::MSG_ERROR);
            return false;
        }
    } else if (driver == CameraDriver::HIKVISION) {
        bool flag_camera = rm::getHikVisionCameraNum(camera_num);
        if (!flag_camera) {
            rm::message("Failed to get HikVision camera number", rm::MSG_ERROR);
            return false;
        }
    } else {
        bool flag_camera = rm::getMindVisionCameraNum(camera_num);
        if (!flag_camera) {
            rm::message("Failed to get camera number", rm::MSG_ERROR);
            return false;
        }
    }

    if (camera_num < 1) {
        rm::message("Invalid camera number: " + std::to_string(camera_num), rm::MSG_ERROR);
        return false;
    }
    if (camera_num > 2) {
        rm::message("Only 1 or 2 cameras are supported, got " +
                    std::to_string(camera_num) + ", using the first two", rm::MSG_WARNING);
        camera_num = 2;
    }

    Data::camera.assign(static_cast<size_t>(camera_num) + 1, nullptr);

    // 单相机：全部使用 Base 参数；双相机：第 1 台 Base，第 2 台 Far
    const bool dual = (camera_num >= 2);
    if (dual && !camera_cfg.contains("Far")) {
        rm::message("Two cameras found but Config.json Camera.Far is missing", rm::MSG_ERROR);
        return false;
    }

    for (int i = 1; i <= camera_num; ++i) {
        const bool is_base = (i == 1);
        nlohmann::json& cfg = is_base ? camera_cfg["Base"] : camera_cfg["Far"];

        if (driver == CameraDriver::UVC && !uvc_devices.empty()) {
            // UVC：Device 字段留空则自动按下标分配
            if (cfgString(cfg, "Device", "").empty()) {
                cfg["Device"] = uvc_devices[static_cast<size_t>(i - 1)];
            }
        }

        const std::string camera_type = cfgString(cfg, "CameraType", "UVC1280_1024");
        const std::string lens_type   = cfgString(cfg, "LensType", "Prime6mm");

        std::vector<double> offset = {0.0, 0.0, 0.0, 0.0, 0.0};
        if (camera_cfg.contains("CameraOffset")) {
            const char* key = is_base ? "Base" : "Far";
            if (camera_cfg["CameraOffset"].contains(key)) {
                offset = camera_cfg["CameraOffset"][key].get<std::vector<double>>();
            }
        } else if ((*param)["Car"]["CameraOffset"].contains(is_base ? "Base" : "Far")) {
            offset = (*param)["Car"]["CameraOffset"][is_base ? "Base" : "Far"].get<std::vector<double>>();
        }

        rm::message("Begin open camera " + std::to_string(i), rm::MSG_NOTE);

        Data::camera[i] = new rm::Camera();
        if (!openCameraByDriver(Data::camera[i], driver, i, cfg, &Data::yaw, &Data::pitch, &Data::roll)) {
            rm::message("Failed to open camera: " + std::to_string(i), rm::MSG_ERROR);
            return false;
        }

        rm::message("Camera " + std::to_string(i) + " width " +
                    std::to_string(Data::camera[i]->width), rm::MSG_NOTE);

        if (!applyLensCalibration(Data::camera[i], camlens, camera_type, lens_type, offset)) {
            return false;
        }

        if (is_base) {
            Data::camera_base = i;
            Data::camera_index = i;
        } else {
            Data::camera_far = i;
        }

        // 推理输入缓冲由 rm::Infer 内部按需分配（纯 host 内存），
        // 这里不再申请 CUDA 锁页/显存
    }

    if (camera_num == 1) Data::camera_far = Data::camera_base;

    return true;
}

bool deinit_camera() {
    for (size_t i = 1; i < Data::camera.size(); i++) {
        if (Data::camera[i] == nullptr) continue;

        if (Data::camera[i]->uvc_capture) {
            rm::closeUVC(Data::camera[i]);
            Data::camera[i]->uvc_capture = false;
        } else {
            // 视频回放线程
            Data::camera[i]->capture_run = false;
            if (Data::camera[i]->capture_thread.joinable()) {
                Data::camera[i]->capture_thread.join();
            }
        }

        delete Data::camera[i];
        Data::camera[i] = nullptr;
    }

    // 关闭对应驱动（UVC 已在上面逐台关闭）
    rm::closeMindVision();
    rm::closeDaHeng();
    rm::closeHikVision();

    rm::message("Camera deinit success", rm::MSG_WARNING);
    return true;
}


void init_serial() {
    int status;
    std::vector<std::string> port_list;
    auto control = Control::get_instance();

    while(true) {

        #if defined(TJURM_HERO)
        status = (int)rm::getSerialPortList(port_list, rm::SERIAL_TYPE_TTY_ACM);
        #endif

        #if defined(TJURM_BALANCE) || defined(TJURM_INFANTRY) || defined(TJURM_DRONSE) || defined(TJURM_SENTRY)
        status = (int)rm::getSerialPortList(port_list, rm::SERIAL_TYPE_TTY_ACM);
        #endif

        if (status != 0 || port_list.empty()) {
            // 犀牛派上 USB-TTL 常见为 /dev/ttyUSB*，板载串口为 /dev/ttyS*
            status = (int)rm::getSerialPortList(port_list, rm::SERIAL_TYPE_TTY_USB);
        }

        if (status != 0 || port_list.empty()) {
            rm::message("Control port list failed", rm::MSG_ERROR);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            port_list.clear();
            continue;
        }

        control->port_name_ = port_list[0];
        status = (int)rm::openSerialPort(control->file_descriptor_, control->port_name_);
        if (status != 0) {
            rm::message("Control port open failed", rm::MSG_ERROR);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            port_list.clear();
            continue;
        }
        if(status == 0) {
            break;
        }
    }
}


void init_attack() {
    #ifdef TJURM_SENTRY
    Data::attack = new rm::Filtrate();
    #endif

    #if defined(TJURM_INFANTRY) || defined(TJURM_BALANCE) || defined(TJURM_HERO) || defined(TJURM_DRONSE)
    Data::attack = new rm::DeadLocker();
    #endif 
}
