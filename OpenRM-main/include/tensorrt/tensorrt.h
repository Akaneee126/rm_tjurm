#ifndef __OPENRM_TENSORRT_TENSORRT_H__
#define __OPENRM_TENSORRT_TENSORRT_H__

#include <NvInfer.h>
#include <NvInferRuntime.h>
#include <NvOnnxParser.h>
#include <string>
#include "structure/stamp.hpp"
#include "tensorrt/logging.h"

// NMS 已经移到与后端无关的推理层，这里保留 include 让老代码继续可用
#include <infer/nms.h>

namespace rm {

bool initTrtOnnx(
    const std::string& onnx_file,
    const std::string& engine_file,
    unsigned int batch_size = 1U
);

bool initTrtEngine(
    const char* input_device_buffer_name,
    const char* output_device_buffer_name,
    const std::string& engine_file,
    nvinfer1::IExecutionContext** context
);

bool initCudaStream(
    cudaStream_t* stream
);

void detectEnqueue(
    const char* input_device_buffer_name,
    const char* output_device_buffer_name,
    float* input_device_buffer,
    float* output_device_buffer,
    nvinfer1::IExecutionContext** context,
    cudaStream_t* stream
);

void detectOutput(
    float* output_host_buffer,
    const float* output_device_buffer,
    cudaStream_t* stream,
    size_t output_struct_size,
    int bboxes_num,
    int batch_size = 1
); 

void detectOutputClassify(
    float* output_host_buffer,
    const float* output_device_buffer,
    cudaStream_t* stream,
    int class_num
); 

void mallocYoloCameraBuffer(
    uint8_t** rgb_host_buffer,
    uint8_t** rgb_device_buffer,
    int rgb_width,
    int rgb_height,
    int batch_size = 1,
    int channels = 3
);


void mallocYoloDetectBuffer(
    float** input_device_buffer,
    float** output_device_buffer,
    float** output_host_buffer,
    int input_width,
    int input_height,
    size_t output_struct_size,
    int bboxes_num,
    int batch_size = 1,
    int channels = 3
);

void mallocClassifyBuffer(
    float** input_host_buffer,
    float** input_device_buffer,
    float** output_device_buffer,
    float** output_host_buffer,
    int input_width,
    int input_height,
    int class_num,
    int channels = 3
);

void freeYoloCameraBuffer(
    uint8_t* rgb_host_buffer,
    uint8_t* rgb_device_buffer
);


void freeYoloDetectBuffer(
    float* input_device_buffer,
    float* output_device_buffer,
    float* output_host_buffer
);

void freeClassifyBuffer(
    float* input_host_buffer,
    float* input_device_buffer,
    float* output_device_buffer,
    float* output_host_buffer
);

void memcpyYoloCameraBuffer(
    uint8_t* rgb_mat_data,
    uint8_t* rgb_host_buffer,
    uint8_t* rgb_device_buffer,
    int rgb_width,
    int rgb_height,
    int channels = 3
);

void memcpyClassifyBuffer(
    uint8_t* mat_data,
    float* input_host_buffer,
    float* input_device_buffer,
    int input_width,
    int input_height,
    int channels = 3
);

// 注意：yoloArmorNMS_* 已移到 <infer/nms.h>（与后端无关，纯 CPU），
// 本头文件在开头已经 include 进来，老代码无需修改。

}

#endif