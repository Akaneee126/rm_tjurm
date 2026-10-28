#include "infer/nms.h"
#include "uniterm/uniterm.h"

#include <algorithm>
#include <cmath>

using namespace rm;

// yolo输出的四点顺序：左上-左下-右下-右上
struct alignas(float) yolofpRaw {
    float pose[8];
    float confidence;
};

struct alignas(float) yolov5Raw {
    float bbox[4];
    float confidence;
};

// 下面这些是"调用即设定"的参数。为了支持 自瞄/打符/四点 多个检测线程并发调用，
// 统一声明为 thread_local（每个线程一份），避免互相覆盖。
static thread_local int classes_num;
static thread_local int colors_num = 3;
static thread_local int yolo_size;

static thread_local float* output_buffer;
static thread_local int output_bboxes_num;

static thread_local float confidence_threshold;
static thread_local float nms_threshold;

static thread_local int input_width;
static thread_local int input_height;
static thread_local int infer_width;
static thread_local int infer_height;

static thread_local float infer_to_input_ratio;
static thread_local float top_move_from_input;
static thread_local float left_move_from_input;

// 输出是否需要在 NMS 前做 sigmoid（模型把激活留在了图外时打开）
static thread_local bool g_apply_sigmoid = false;

void rm::yoloSetSigmoid(bool enable) {
    g_apply_sigmoid = enable;
}

// 把可能为 logits 的通道值变成概率
static inline float nms_act(float x) {
    if (!g_apply_sigmoid) return x;
    return 1.0f / (1.0f + std::exp(-x));
}


static void nms_set_ratio() {
    float width_ratio = (float)input_width / (float)infer_width;
    float height_ratio = (float)input_height / (float)infer_height;

    top_move_from_input = ((float)infer_height * width_ratio - (float)input_height) / 2.f;
    left_move_from_input = ((float)infer_width * height_ratio - (float)input_width) / 2.f;

	// 根据缩放比最大的边设置缩放比例
    if (width_ratio > height_ratio) {
        infer_to_input_ratio = width_ratio;
        left_move_from_input = 0;
    } else {
        infer_to_input_ratio = height_ratio;
        top_move_from_input = 0;
    }
}

// 从yolo推理的四点，转化为外接矩形
static cv::Rect nms_get_rect(yolofpRaw* yolo_raw) {
    float min_x = std::min(std::min(yolo_raw->pose[0], yolo_raw->pose[2]), std::min(yolo_raw->pose[4], yolo_raw->pose[6]));
    float max_x = std::max(std::max(yolo_raw->pose[0], yolo_raw->pose[2]), std::max(yolo_raw->pose[4], yolo_raw->pose[6]));
    float min_y = std::min(std::min(yolo_raw->pose[1], yolo_raw->pose[3]), std::min(yolo_raw->pose[5], yolo_raw->pose[7]));
    float max_y = std::max(std::max(yolo_raw->pose[1], yolo_raw->pose[3]), std::max(yolo_raw->pose[5], yolo_raw->pose[7]));

    float left = min_x * infer_to_input_ratio - left_move_from_input;
    float top = min_y * infer_to_input_ratio - top_move_from_input;
    float width = (max_x - min_x) * infer_to_input_ratio;
    float height = (max_y - min_y) * infer_to_input_ratio;

    return cv::Rect(round(left), round(top), round(width), round(height));
}

// 四点顺序：不同模型导出的原始顺序不一样。
//   0 = 旧格式（上交/北洋旧模型）：原始顺序经 {0,6,2,4} 重排后得到 左上-左下-右下-右上
//   1 = 行优先格式（0526 模型实测）：原始顺序是 左上-右上-左下-右下，需要 {0,4,6,2}
// 输出的四点顺序统一是 左上-左下-右下-右上（与后续 PnP 约定一致）。
static thread_local int g_pt_x[4] = {0, 6, 2, 4};

void rm::yoloSetPointOrder(int mode) {
    // 输出目标：左上-左下-右下-右上
    if (mode == 3) {
        // 原始就是 左上-左下-右下-右上（0526 模型实测），直接用
        g_pt_x[0] = 0; g_pt_x[1] = 2; g_pt_x[2] = 4; g_pt_x[3] = 6;
    } else if (mode == 1) {
        // 原始：左上-右上-左下-右下
        g_pt_x[0] = 0; g_pt_x[1] = 4; g_pt_x[2] = 6; g_pt_x[3] = 2;
    } else if (mode == 2) {
        // 原始：左上-右上-右下-左下（顺时针闭环）
        g_pt_x[0] = 0; g_pt_x[1] = 6; g_pt_x[2] = 4; g_pt_x[3] = 2;
    } else {
        // 旧模型默认
        g_pt_x[0] = 0; g_pt_x[1] = 6; g_pt_x[2] = 2; g_pt_x[3] = 4;
    }
}

static std::vector<cv::Point2f> nms_get_fp(yolofpRaw* yolo_raw) {
    std::vector<cv::Point2f> four_points;

    for(int i = 0; i < 4; i++) {
        double x = yolo_raw->pose[g_pt_x[i]] * infer_to_input_ratio - left_move_from_input;
        double y = yolo_raw->pose[g_pt_x[i] + 1] * infer_to_input_ratio - top_move_from_input;
        if(x < 0 || x >= input_width || y < 0 || y >= input_height) {
            return std::vector<cv::Point2f>();
        }
        four_points.push_back(cv::Point2f(x, y));
    }
    return four_points;
}

// 从yolo推理的框，转化为opencv的Rect
static cv::Rect nms_get_rect(yolov5Raw* yolo_raw) {
    float x = yolo_raw->bbox[0];
    float y = yolo_raw->bbox[1];
    float w = yolo_raw->bbox[2];
    float h = yolo_raw->bbox[3];

    float half_w = w / 2.f;
    float half_h = h / 2.f;

    float left = (x - half_w) * infer_to_input_ratio - left_move_from_input;
    float top = (y - half_h) * infer_to_input_ratio - top_move_from_input;
    float right = (x + half_w) * infer_to_input_ratio - left_move_from_input;
    float bottom = (y + half_h) * infer_to_input_ratio - top_move_from_input;

    return cv::Rect(round(left), round(top), round(right - left), round(bottom - top));
}

// 上古传承代码，计算iou交并比的函数
static float nms_calcu_iou(cv::Rect box1, cv::Rect box2) {

    // 计算重叠区域左上角坐标
    int x1 = std::max(box1.x, box2.x);
    int y1 = std::max(box1.y, box2.y);
    // 计算重叠区域右下角坐标
    int x2 = std::min(box1.x + box1.width, box2.x + box2.width);
    int y2 = std::min(box1.y + box1.height, box2.y + box2.height);
    // 计算重叠区域宽高
    int w = std::max(0, x2 - x1 + 1);
    int h = std::max(0, y2 - y1 + 1);

    // 计算交并集面积, 1e-5防止除以0
    float over_area = w * h;
    float union_area = box1.width * box1.height + box2.width * box2.height - over_area + 1e-5;

    return over_area / union_area;
}

static void nms_select_confidence_fp(std::vector<YoloRect> &list) {
    // 遍历yolov5推理结果
    for (int i = 0; i < output_bboxes_num; i++) {
		
        // 使用结构体截断Raw数据，其float长度为：8 + 1 + 类别数
        yolofpRaw* yolo_raw = (yolofpRaw*)(output_buffer + i * yolo_size);
        float iou_confidence = nms_act(yolo_raw->confidence);
        if (iou_confidence < confidence_threshold) continue;


        // 对所有类别的置信度进行筛选，找到最高的
        int class_index = -1;
        float class_confidence = 0;
        for (int i = 0; i < classes_num; i++) {
            float* yolo_float = (float*)yolo_raw;
            float confidence = nms_act(yolo_float[9 + i]) * iou_confidence;
            if (confidence > class_confidence && confidence > confidence_threshold) {
                class_index = i;
                class_confidence = confidence;
            }
        }
        if(class_index == -1) continue;

        // 对贴近边缘的框进行过滤
        bool flag_pose = true;
        for(int i = 0; i < 4; i++) {
            if(yolo_raw->pose[2 * i] < 1e-3 || yolo_raw->pose[2 * i] > (infer_width - 1.001) ||
               yolo_raw->pose[2 * i + 1] < 1e-3 || yolo_raw->pose[2 * i + 1] > (infer_height - 1.001)) {
                flag_pose = false;
                break;
            }
        }
        if(!flag_pose) continue;


        // 创建推理框结构体并赋值
        YoloRect detection_rect;
        detection_rect.confidence = class_confidence;
        detection_rect.class_id = class_index;
        detection_rect.box = nms_get_rect(yolo_raw);
        detection_rect.four_points = nms_get_fp(yolo_raw);

        // 将推理框推入vector?
        if(detection_rect.four_points.size() != 4) continue;
        list.push_back(detection_rect);
    }
}

// 筛选置信度并创建推理框对象
static void nms_select_confidence_v5(std::vector<YoloRect> &list) {
    for (int i = 0; i < output_bboxes_num; i++) {
		
        // 使用结构体截断Raw数据，其float长度为：4 + 1 + 类别数
        yolov5Raw* yolo_raw = (yolov5Raw*)(output_buffer + i * yolo_size);
        float iou_confidence = nms_act(yolo_raw->confidence);
        if(iou_confidence < confidence_threshold) continue;

        // 对所有类别的置信度进行筛选，找到最高的
        int class_index = -1;
        float class_confidence = 0;
        for (int i = 0; i < classes_num; i++) {
            float* yolo_float = (float*)yolo_raw;
            float confidence = nms_act(yolo_float[5 + i]) * iou_confidence;
            if (confidence > class_confidence && confidence > confidence_threshold) {
                class_index = i;
                class_confidence = confidence;
            }
        }
        if(class_index == -1) continue;

        // 创建推理框结构体并赋值
        YoloRect detection_rect;
        detection_rect.confidence = class_confidence;
        detection_rect.class_id = class_index;
        detection_rect.box = nms_get_rect(yolo_raw);

        // 将推理框推入vector?
        list.push_back(detection_rect);
    }
}

static void nms_select_confidence_fpx(std::vector<YoloRect> &list) {
    // 遍历推理结果
    for (int i = 0; i < output_bboxes_num; i++) {
		
        // 使用结构体截断Raw数据，其float长度为：8 + 1 + 颜色数+ 类别数
        yolofpRaw* yolo_raw = (yolofpRaw*)(output_buffer + i * yolo_size);
        float iou_confidence = nms_act(yolo_raw->confidence);
        if (iou_confidence < confidence_threshold) continue;


        // 对颜色置信度进行筛选
        int color_index = -1;
        float color_confidence = 0;
        for (int i = 0; i < colors_num; i++) {
            float* yolo_float = (float*)yolo_raw;
            float confidence = nms_act(yolo_float[9 + i]) * iou_confidence;
            if (confidence > color_confidence && confidence > confidence_threshold) {
                color_index = i;
                color_confidence = confidence;
            }
        }
        if(color_index == -1) continue;


        // 对所有类别的置信度进行筛选
        int class_index = -1;
        float class_confidence = 0;
        for (int i = 0; i < classes_num; i++) {
            float* yolo_float = (float*)yolo_raw;
            float confidence = nms_act(yolo_float[9 + colors_num + i]) * iou_confidence;
            if (confidence > class_confidence && confidence > confidence_threshold) {
                class_index = i;
                class_confidence = confidence;
            }
        }
        if(class_index == -1) continue;


        // 对贴近边缘的框进行过滤
        bool flag_pose = true;
        for(int i = 0; i < 4; i++) {
            if(yolo_raw->pose[2 * i] < 1e-3 || yolo_raw->pose[2 * i] > (infer_width - 1.001) ||
               yolo_raw->pose[2 * i + 1] < 1e-3 || yolo_raw->pose[2 * i + 1] > (infer_height - 1.001)) {
                flag_pose = false;
                break;
            }
        }
        if(!flag_pose) continue;


        // 创建推理框结构体并赋值
        YoloRect detection_rect;
        detection_rect.confidence = class_confidence;
        detection_rect.color_id = color_index;
        detection_rect.class_id = class_index;
        detection_rect.box = nms_get_rect(yolo_raw);
        detection_rect.four_points = nms_get_fp(yolo_raw);

        // 将推理框推入vector?
        if(detection_rect.four_points.size() != 4) continue;
        list.push_back(detection_rect);
    }
}

// 按置信度排序
static void nms_sort_confidence(std::vector<YoloRect> &list) {
    if (list.size() <= 1) return;
    std::sort(
        list.begin(),
        list.end(),
        [](const YoloRect& a, const YoloRect& b) {
            return a.confidence > b.confidence;
        }
    );
}

static void nms_select_iou(std::vector<YoloRect> &list) {
    // 创建保留的推理框vector，用于最后替换原推理框vector
    std::vector<YoloRect> retained_list;

    // 对所有类别进行遍历
    if (list.size() <= 1) return;
        
    // 要被决定是否保留的关注框，从当前类别的第一个开始
    YoloRect focus_rect = list[0];

    // 为保留推理框vector创建新的对象，将第一个框推进去
    retained_list = std::vector<YoloRect>();
    retained_list.push_back(focus_rect);

    // 对类内所有推理框进行遍历
    for (size_t focus_index = 1; focus_index < list.size(); focus_index++) {

        // 对所有保留框进行遍历
        bool avaliable_rect = true;
        for (size_t retained_index = 0; retained_index < retained_list.size(); retained_index++) {

            // 计算当前遍历到的保留框和关注框的iou
            float iou = nms_calcu_iou(
                list[focus_index].box,
                retained_list[retained_index].box
            );

            // 已排序说明关注框置信度小于保留框，iou过大则舍弃关注框
            if (iou > nms_threshold) {
                avaliable_rect = false;
                break;
            }
        }

        // 遍历结束后，标签仍为true，则该框可被保留
        if(avaliable_rect) {
            retained_list.push_back(list[focus_index]);
        }
    }

    // 替换当前类的推理框vector为保留框vector
    list = retained_list;
}


std::vector<YoloRect> rm::yoloArmorNMS_FP(
    float* _output_host_buffer,
    int _output_bboxes_num,
    int _classes_num,
    float _confidence_threshold,
    float _nms_threshold,
    int _input_width,
    int _input_height,
    int _infer_width,
    int _infer_height
) {
    output_buffer = _output_host_buffer;
    output_bboxes_num = _output_bboxes_num;
    classes_num = _classes_num;
    confidence_threshold = _confidence_threshold;
    nms_threshold = _nms_threshold;
    input_width = _input_width;
    input_height = _input_height;
    infer_width = _infer_width;
    infer_height = _infer_height;
    yolo_size = 9 + classes_num;
    nms_set_ratio();

    std::vector<YoloRect> detection_list;
    nms_select_confidence_fp(detection_list);
    nms_sort_confidence(detection_list);
    nms_select_iou(detection_list);
    return detection_list;
}

std::vector<YoloRect> rm::yoloArmorNMS_V5(
    float* _output_host_buffer,
    int _output_bboxes_num,
    int _classes_num,
    float _confidence_threshold,
    float _nms_threshold,
    int _input_width,
    int _input_height,
    int _infer_width,
    int _infer_height
) {
    output_buffer = _output_host_buffer;
    output_bboxes_num = _output_bboxes_num;
    classes_num = _classes_num;
    confidence_threshold = _confidence_threshold;
    nms_threshold = _nms_threshold;
    input_width = _input_width;
    input_height = _input_height;
    infer_width = _infer_width;
    infer_height = _infer_height;
    yolo_size = 5 + classes_num;
    nms_set_ratio();

    std::vector<YoloRect> detection_list;
    nms_select_confidence_v5(detection_list);
    nms_sort_confidence(detection_list);
    nms_select_iou(detection_list);
    return detection_list;
}

std::vector<YoloRect> rm::yoloArmorNMS_FPX(
    float* _output_host_buffer,
    int _output_bboxes_num,
    int _classes_num,
    float _confidence_threshold,
    float _nms_threshold,
    int _input_width,
    int _input_height,
    int _infer_width,
    int _infer_height,
    int _colors_num
) {
    output_buffer = _output_host_buffer;
    output_bboxes_num = _output_bboxes_num;
    classes_num = _classes_num;
    colors_num = _colors_num;
    confidence_threshold = _confidence_threshold;
    nms_threshold = _nms_threshold;
    input_width = _input_width;
    input_height = _input_height;
    infer_width = _infer_width;
    infer_height = _infer_height;
    yolo_size = 9 + colors_num + classes_num;
    nms_set_ratio();

    std::vector<YoloRect> detection_list;
    nms_select_confidence_fpx(detection_list);
    nms_sort_confidence(detection_list);
    nms_select_iou(detection_list);
    return detection_list;
}
