#ifndef __OPENRM_H__
#define __OPENRM_H__

#include <VersionConfig.h>

#include <attack/attack.h>
#include <attack/deadlocker.h>
#include <attack/freshcenter.h>
#include <attack/filtrate.h>

#include <kalman/kalman.h>

#include <pointer/pointer.h>

#include <solver/solvepnp.h>
#include <solver/ternary.hpp>

#include <structure/cyclequeue.hpp>
#include <structure/slidestd.hpp>
#include <structure/swapbuffer.hpp>
#include <structure/speedqueue.hpp>

#include <structure/enums.hpp>
#include <structure/stamp.hpp>
#include <structure/camera.hpp>
#include <structure/shm.hpp>

#include <infer/infer.h>
#include <infer/nms.h>

// TensorRT 头文件只在本机装了 CUDA/NvInfer 时存在，
// 无 Nvidia 硬件（例如 AidLux 犀牛派）时自动跳过
#if defined(__has_include)
#  if __has_include(<NvInfer.h>)
#    include <tensorrt/tensorrt.h>
#  endif
#else
#  include <tensorrt/tensorrt.h>
#endif

#include <uniterm/uniterm.h>

#include <utils/timer.h>
#include <utils/delay.h>
#include <utils/tf.h>
#include <utils/serial.h>
#include <utils/print.h>

#include <video/video.h>

#endif