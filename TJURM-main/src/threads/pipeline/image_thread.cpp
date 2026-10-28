#include "threads/pipeline.h"
#include <string>
#include <cstdio>
#include <thread>
#include <chrono>

static std::string imshow_msg_buffer;

void Pipeline::imshow(std::shared_ptr<rm::Frame> frame_show) {
    this->imshow_register_ = frame_show;
    this->imshow_in_ = true;
}

void Pipeline::imshow(std::shared_ptr<rm::Frame> frame_show, std::string& frame_msg) {
    this->imshow_register_ = frame_show;
    imshow_msg_buffer = frame_msg;
    this->imshow_in_ = true;
}

void Pipeline::image_thread() {
    auto param = Param::get_instance();
    auto garage = Garage::get_instance();

    std::string imwrite_dir;
    imwrite_dir = (*param)["Camera"]["DebugSaveDir"];
    int fps = (*param)["Debug"]["ImageThread"]["FPS"];
    bool light_flag = (*param)["Debug"]["ImageThread"]["Light"];
    double scale = (*param)["Debug"]["ImageThread"]["Scale"];

    int delay = (fps > 0) ? (1000 / fps) : 40;

    while(true) {
        while(!imshow_in_) {}

        cv::Mat image = *(this->imshow_register_->image);

        if (image.empty()) {
            std::cout << "image_thread: empty image" << std::endl;
            continue;
        } else if (image.type() != CV_8UC1 && image.type() != CV_8UC3 && image.type() != CV_32FC1 && image.type() != CV_32FC3) {
            std::cout << "image_thread: unsupported image type" << std::endl;
            continue;
        } else {
            cv::Mat resized_image;
            cv::resize(image, resized_image, cv::Size(image.cols * scale, image.rows * scale));

            if (light_flag) {
                std::vector<cv::Mat> channels;
                cv::split(resized_image, channels);
                cv::equalizeHist(channels[0], channels[0]);
                cv::equalizeHist(channels[1], channels[1]);
                cv::equalizeHist(channels[2], channels[2]);
                cv::merge(channels, resized_image);
            }

            if (Data::imshow_flag || Data::imwrite_flag) {
                // 中心十字准星
                int centerX = resized_image.cols / 2;
                int centerY = resized_image.rows / 2;
                int size = 10;
                cv::Scalar green(0, 255, 0);

                cv::line(resized_image, cv::Point(centerX - size, centerY),
                         cv::Point(centerX + size, centerY), green, 1);
                cv::line(resized_image, cv::Point(centerX, centerY - size),
                         cv::Point(centerX, centerY + size), green, 1);
                cv::circle(resized_image, cv::Point(centerX, centerY), 2, green, 1);

                // 把检测结果画出来（原版这里只画准星，看不到框）
                for (const auto& yolo : this->imshow_register_->yolo_list) {
                    if (yolo.four_points.size() == 4) {
                        std::vector<cv::Point> poly;
                        for (const auto& p : yolo.four_points) {
                            poly.push_back(cv::Point((int)(p.x * scale), (int)(p.y * scale)));
                        }
                        // 蓝方 / 红方用不同颜色
                        cv::Scalar color = (yolo.color_id == 1) ? cv::Scalar(0, 0, 255)
                                                                : cv::Scalar(255, 128, 0);
                        cv::polylines(resized_image, poly, true, color, 2);
                        char label[32];
                        snprintf(label, sizeof(label), "%d c%.2f", yolo.class_id, yolo.confidence);
                        cv::putText(resized_image, label, poly[0], cv::FONT_HERSHEY_SIMPLEX,
                                    0.4, color, 1);
                    } else if (yolo.box.width > 0) {
                        cv::Rect r((int)(yolo.box.x * scale), (int)(yolo.box.y * scale),
                                   (int)(yolo.box.width * scale), (int)(yolo.box.height * scale));
                        cv::rectangle(resized_image, r, cv::Scalar(255, 128, 0), 2);
                    }
                }

                std::vector<std::string> lines;
                if(Data::target_id != rm::ARMOR_ID_UNKNOWN) {
                    auto obj = garage->getObj(Data::target_id);
                    obj->getState(lines);
                }
                rm::displayStrVecOnImage(resized_image, lines);
            }

            if (Data::imshow_flag) {
                cv::imshow("tjurm2024frame", resized_image);
                cv::waitKey(1);
            }

            if (Data::imwrite_flag) {
                std::string path = imwrite_dir + "/" + getMsStr() + ".jpg";
                cv::imwrite(path, resized_image);
            }

            imshow_in_ = false;

            std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        }
    }
}
