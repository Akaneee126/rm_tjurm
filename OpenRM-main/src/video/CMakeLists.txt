add_library(
    openrm_video
        SHARED
)

# 查找 gxiapi 动态链接库
find_library(GXIAPI_LIB
    NAMES gxiapi
)

# 查找 MindVision 动态链接库
find_library(MVSDK_LIB
    NAMES MVSDK
)

# 查找 HikVision 动态链接库
find_library(MVCC_LIB
    NAMES MvCameraControl
)

# 检查库是否找到
if(GXIAPI_LIB)
    message(STATUS "Found DaHeng: ${GXIAPI_LIB}")
    set(HAVE_GXIAPI TRUE)
else()
    message(STATUS "Could NOT found DaHeng libgxiapi.so, disable DaHeng camera support.")
    set(HAVE_GXIAPI FALSE)
endif()

if(MVSDK_LIB)
    message(STATUS "Found MindVision: ${MVSDK_LIB}")
    set(HAVE_MVSDK TRUE)
else()
    message(STATUS "Could NOT found MindVision libMvCameraControl.so, disable MindVision camera support.")
    set(HAVE_MVSDK FALSE)
endif()

if(MVCC_LIB)
    message(STATUS "Found HikVision: ${MVCC_LIB}")
    set (HAVE_MVCC TRUE)
else()
    message(STATUS "Could NOT found HikVision Lib, disable HikVision camera support.")
    set (HAVE_MVCC FALSE)
endif()

target_sources(
    openrm_video
        PRIVATE
        ${CMAKE_SOURCE_DIR}/src/video/uvc.cpp
        ${CMAKE_SOURCE_DIR}/src/video/tools.cpp
        ${CMAKE_SOURCE_DIR}/src/video/stub_camera.cpp
        $<IF:$<BOOL:${HAVE_GXIAPI}>,${CMAKE_SOURCE_DIR}/src/video/daheng.cpp,>
        $<IF:$<BOOL:${HAVE_MVSDK}>,${CMAKE_SOURCE_DIR}/src/video/mindvision.cpp,>
        $<IF:$<BOOL:${HAVE_MVCC}>,${CMAKE_SOURCE_DIR}/src/video/hikvision.cpp,>
)

# stub_camera.cpp 依据这些宏决定哪些驱动需要空实现
if (HAVE_GXIAPI)
    target_compile_definitions(openrm_video PRIVATE HAVE_GXIAPI=1)
endif()
if (HAVE_MVSDK)
    target_compile_definitions(openrm_video PRIVATE HAVE_MVSDK=1)
endif()
if (HAVE_MVCC)
    target_compile_definitions(openrm_video PRIVATE HAVE_MVCC=1)
endif()

target_include_directories(
    openrm_video
        PRIVATE
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include/openrm>
)

target_link_libraries(
    openrm_video
        PRIVATE
        $<IF:$<BOOL:${HAVE_GXIAPI}>,${GXIAPI_LIB},>
        $<IF:$<BOOL:${HAVE_MVSDK}>,${MVSDK_LIB},>
        $<IF:$<BOOL:${HAVE_MVCC}>,${MVCC_LIB},>
)
