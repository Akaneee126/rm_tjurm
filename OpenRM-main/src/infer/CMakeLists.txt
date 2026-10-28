add_library(
    openrm_infer
        SHARED
)

set(
    OPENRM_INFER_SOURCES
        ${CMAKE_SOURCE_DIR}/src/infer/infer.cpp
        ${CMAKE_SOURCE_DIR}/src/infer/backend_null.cpp
        ${CMAKE_SOURCE_DIR}/src/infer/nms.cpp
        ${CMAKE_SOURCE_DIR}/src/infer/nmsV5C36.cpp
)

if (OPENRM_WITH_AIDLITE)
    list(APPEND OPENRM_INFER_SOURCES ${CMAKE_SOURCE_DIR}/src/infer/backend_aidlite.cpp)
endif()

if (OPENRM_WITH_TENSORRT)
    list(APPEND OPENRM_INFER_SOURCES ${CMAKE_SOURCE_DIR}/src/infer/backend_tensorrt.cpp)
endif()

target_sources(
    openrm_infer
        PRIVATE
        ${OPENRM_INFER_SOURCES}
)

target_include_directories(
    openrm_infer
        PRIVATE
        ${CMAKE_SOURCE_DIR}/src/infer
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include/openrm>
)

target_link_libraries(
    openrm_infer
        PRIVATE
        openrm_uniterm
)

if (OPENRM_WITH_AIDLITE)
    target_compile_definitions(openrm_infer PRIVATE OPENRM_WITH_AIDLITE)
    target_include_directories(openrm_infer PRIVATE ${OPENRM_AIDLITE_INCLUDE_DIR})
    target_link_libraries(openrm_infer PRIVATE ${OPENRM_AIDLITE_LIBRARY})
    message(STATUS "openrm_infer: AidLite backend enabled (${OPENRM_AIDLITE_LIBRARY})")
endif()

if (OPENRM_WITH_TENSORRT)
    target_compile_definitions(openrm_infer PRIVATE OPENRM_WITH_TENSORRT)
    target_link_libraries(openrm_infer PRIVATE openrm_tensorrt cudart)
    message(STATUS "openrm_infer: TensorRT backend enabled")
endif()
