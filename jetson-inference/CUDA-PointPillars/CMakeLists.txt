cmake_minimum_required(VERSION 2.8.12)
project(pointpillar_rt)

# =========================
# 옵션
# =========================
option(WITH_LIVOX   "Enable Livox SDK v1 realtime input" OFF)
option(WITH_LIVOX2  "Enable Livox SDK v2 realtime input" OFF)
option(WITH_OPENCV  "Enable OpenCV (viz/GUI)"            OFF)

# 둘 다 ON이면 에러
if(WITH_LIVOX AND WITH_LIVOX2)
  message(FATAL_ERROR "Enable either WITH_LIVOX (SDK v1) OR WITH_LIVOX2 (SDK v2), not both.")
endif()

# 기본 빌드 타입
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -Wno-deprecated-declarations")
add_compile_options(-W -std=c++14)

# =========================
# CUDA (FindCUDA)
# =========================
find_package(CUDA REQUIRED)

# Jetson/x86_64 타깃 경로
if(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
  set(CUDA_INSTALL_TARGET_DIR targets/aarch64-linux)
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
  set(CUDA_INSTALL_TARGET_DIR targets/x86_64-linux)
else()
  message(WARNING "Unknown arch: ${CMAKE_SYSTEM_PROCESSOR} (fallback x86_64)")
  set(CUDA_INSTALL_TARGET_DIR targets/x86_64-linux)
endif()

set(CUDA_TOOLKIT_ROOT_DIR /usr/local/cuda CACHE PATH "CUDA toolkit root")
set(CUDA_INCLUDE_DIRS ${CUDA_TOOLKIT_ROOT_DIR}/include)

# SM 아키텍처 (필요 시 -DSM_LIST="72;87")
if(NOT DEFINED SM_LIST)
  set(SM_LIST 72 87) # Xavier(72), Orin(87)
endif()

# GENCODE
set(GENCODE "")
foreach(sm ${SM_LIST})
  set(GENCODE ${GENCODE} -gencode arch=compute_${sm},code=sm_${sm})
endforeach()
list(GET SM_LIST -1 HIGHEST_SM)
set(GENCODE ${GENCODE} -gencode arch=compute_${HIGHEST_SM},code=compute_${HIGHEST_SM})

# NVCC 플래그
set(CUDA_NVCC_FLAGS ${CUDA_NVCC_FLAGS}
  -ccbin ${CMAKE_CXX_COMPILER}
  ${GENCODE}
  -Xlinker -rpath=/usr/lib/aarch64-linux-gnu/
  -Xlinker -L/usr/lib/aarch64-linux-gnu/
  -Xlinker -L${CUDA_TOOLKIT_ROOT_DIR}/${CUDA_INSTALL_TARGET_DIR}/lib
)

# =========================
# TensorRT
# =========================
set(TENSORRT_ROOT "" CACHE PATH "TensorRT root dir (optional)")

find_path(TENSORRT_INCLUDE_DIR
  NAMES NvInfer.h
  HINTS ${TENSORRT_ROOT}/include /usr/include/aarch64-linux-gnu /usr/include/x86_64-linux-gnu /usr/include
)
find_library(TENSORRT_LIB_INFER      NAMES nvinfer
  HINTS ${TENSORRT_ROOT}/lib /usr/lib/aarch64-linux-gnu /usr/lib/x86_64-linux-gnu /usr/lib)
find_library(TENSORRT_LIB_ONNXPARSER NAMES nvonnxparser
  HINTS ${TENSORRT_ROOT}/lib /usr/lib/aarch64-linux-gnu /usr/lib/x86_64-linux-gnu /usr/lib)
find_library(TENSORRT_LIB_PLUGIN     NAMES nvinfer_plugin
  HINTS ${TENSORRT_ROOT}/lib /usr/lib/aarch64-linux-gnu /usr/lib/x86_64-linux-gnu /usr/lib)

if(NOT TENSORRT_INCLUDE_DIR OR NOT TENSORRT_LIB_INFER OR NOT TENSORRT_LIB_ONNXPARSER OR NOT TENSORRT_LIB_PLUGIN)
  message(FATAL_ERROR "TensorRT not found. (힌트: -DTENSORRT_ROOT=/path/to/TensorRT)")
endif()

include_directories(${TENSORRT_INCLUDE_DIR})
link_directories(/usr/lib/aarch64-linux-gnu ${CUDA_TOOLKIT_ROOT_DIR}/${CUDA_INSTALL_TARGET_DIR}/lib)

# =========================
# OpenCV (옵션)
# =========================
if(WITH_OPENCV)
  find_package(OpenCV QUIET COMPONENTS core imgproc highgui)
  if(NOT OpenCV_FOUND)
    message(FATAL_ERROR "WITH_OPENCV=ON 이지만 OpenCV를 찾을 수 없습니다. -DOpenCV_DIR 힌트 필요")
  endif()
  message(STATUS "OpenCV: ${OpenCV_VERSION}")
endif()

# =========================
# Threads
# =========================
find_package(Threads REQUIRED)

# =========================
# Livox SDK v1 (옵션)
# -DLIVOX_SDK_INCLUDE=/path/to/Livox-SDK/sdk_core/include
# -DLIVOX_SDK_LIB_FILE=/path/to/liblivox_sdk_static.a (또는 .so)
# =========================
set(LIVOX_SDK_INCLUDE "" CACHE PATH "Livox SDK v1 include dir (contains livox_sdk.h)")
set(LIVOX_SDK_LIB_FILE "" CACHE FILEPATH "Livox SDK v1 library file (.a or .so)")

set(_LIVOX_OK FALSE)
if(WITH_LIVOX)
  if(LIVOX_SDK_INCLUDE AND EXISTS "${LIVOX_SDK_INCLUDE}/livox_sdk.h")
    set(_LIVOX_INC "${LIVOX_SDK_INCLUDE}")
  else()
    message(FATAL_ERROR "livox_sdk.h 를 찾지 못했습니다. -DLIVOX_SDK_INCLUDE=/path/to/sdk_core/include")
  endif()

  if(LIVOX_SDK_LIB_FILE AND EXISTS "${LIVOX_SDK_LIB_FILE}")
    set(_LIVOX_LIB "${LIVOX_SDK_LIB_FILE}")
  else()
    find_library(_LIVOX_LIB livox_sdk HINTS /usr/local/lib /usr/lib/aarch64-linux-gnu /usr/lib)
    if(NOT _LIVOX_LIB)
      message(FATAL_ERROR "Livox SDK v1 라이브러리를 찾지 못했습니다. -DLIVOX_SDK_LIB_FILE=/path/to/liblivox_sdk_static.a")
    endif()
  endif()

  set(_LIVOX_OK TRUE)
  message(STATUS "Livox v1 include: ${_LIVOX_INC}")
  message(STATUS "Livox v1 lib    : ${_LIVOX_LIB}")
endif()

# =========================
# Livox SDK v2 (옵션)
# -DLIVOX2_INCLUDE=/livox_ws/Livox-SDK2/include
# -DLIVOX2_LIB_FILE=/livox_ws/Livox-SDK2/build/sdk_core/liblivox_lidar_sdk_static.a
# =========================
set(LIVOX2_INCLUDE  "" CACHE PATH "Livox SDK v2 include dir (contains livox_lidar_api.h)")
set(LIVOX2_LIB_FILE "" CACHE FILEPATH "Livox SDK v2 library file (.a or .so)")

set(_LIVOX2_OK FALSE)
if(WITH_LIVOX2)
  if(LIVOX2_INCLUDE AND EXISTS "${LIVOX2_INCLUDE}/livox_lidar_api.h")
    set(_LIVOX2_INC "${LIVOX2_INCLUDE}")
  else()
    message(FATAL_ERROR "livox_lidar_api.h 를 찾지 못했습니다. -DLIVOX2_INCLUDE=/path/to/Livox-SDK2/include")
  endif()

  if(LIVOX2_LIB_FILE AND EXISTS "${LIVOX2_LIB_FILE}")
    set(_LIVOX2_LIB "${LIVOX2_LIB_FILE}")
  else()
    find_library(_LIVOX2_LIB livox_lidar_sdk HINTS /usr/local/lib /usr/lib/aarch64-linux-gnu /usr/lib)
    if(NOT _LIVOX2_LIB)
      message(FATAL_ERROR "Livox SDK v2 라이브러리를 찾지 못했습니다. -DLIVOX2_LIB_FILE=/path/to/liblivox_lidar_sdk_static.a")
    endif()
  endif()

  set(_LIVOX2_OK TRUE)
  message(STATUS "Livox v2 include: ${_LIVOX2_INC}")
  message(STATUS "Livox v2 lib    : ${_LIVOX2_LIB}")
endif()

# =========================
# 공용 인클루드
# =========================
include_directories(
  ${CUDA_INCLUDE_DIRS}
  include
)

# =========================
# 소스(명시적으로 지정)
# =========================
set(PP_CPP
  src/pointpillar.cpp
  src/preprocess.cpp
  src/postprocess.cpp
  src/pillarScatter.cpp
)

set(PP_CU
  src/preprocess_kernels.cu
  src/pillarScatterKernels.cu
  src/postprocess_kernels.cu
)

# =========================
# 공용 라이브러리 구성
# =========================
include(FindCUDA)

# (1) CUDA 커널 라이브러리
cuda_add_library(pointpillar_kernels STATIC ${PP_CU})

# (2) C++ 코어 라이브러리
add_library(pointpillar_core STATIC ${PP_CPP})
target_link_libraries(pointpillar_core PUBLIC
  pointpillar_kernels
  ${TENSORRT_LIB_INFER}
  ${TENSORRT_LIB_ONNXPARSER}
  ${TENSORRT_LIB_PLUGIN}
  cudart cublas cublasLt curand
  Threads::Threads dl m
)
target_include_directories(pointpillar_core PUBLIC
  ${TENSORRT_INCLUDE_DIR}
  ${CUDA_INCLUDE_DIRS}
  ${PROJECT_SOURCE_DIR}/include
)
if(WITH_OPENCV)
  target_link_libraries(pointpillar_core PUBLIC ${OpenCV_LIBS})
endif()

# =========================
# 실행파일: pointpillar_rt (오프라인 배치)
# =========================
cuda_add_executable(${PROJECT_NAME} main.cpp)
# plain 시그니처(한 번 호출)
target_link_libraries(${PROJECT_NAME}
  -Wl,--whole-archive
    pointpillar_core
    pointpillar_kernels
  -Wl,--no-whole-archive
  nvinfer nvonnxparser nvinfer_plugin
  cudart cublas cublasLt curand
  Threads::Threads dl m
  ${OpenCV_LIBS}
)
if(WITH_OPENCV)
  target_compile_definitions(${PROJECT_NAME} PRIVATE WITH_OPENCV=1)
endif()
set_target_properties(${PROJECT_NAME} PROPERTIES
  BUILD_RPATH "/usr/lib/aarch64-linux-gnu;${CUDA_TOOLKIT_ROOT_DIR}/${CUDA_INSTALL_TARGET_DIR}/lib"
  INSTALL_RPATH "/usr/lib/aarch64-linux-gnu;${CUDA_TOOLKIT_ROOT_DIR}/${CUDA_INSTALL_TARGET_DIR}/lib"
)

# =========================
# 실행파일: livox_test (SDK1 단독 체크)
# =========================
if(WITH_LIVOX)
  add_executable(livox_test tool/livox_test.cpp)
  target_include_directories(livox_test PRIVATE ${_LIVOX_INC})
  target_link_libraries(livox_test PRIVATE ${_LIVOX_LIB} Threads::Threads dl m)
  target_compile_definitions(livox_test PRIVATE WITH_LIVOX=1)
  set_target_properties(livox_test PROPERTIES
    BUILD_RPATH "/usr/lib/aarch64-linux-gnu"
    INSTALL_RPATH "/usr/lib/aarch64-linux-gnu")

  # 실행파일: livox_live_mid70 (MID-70 / Livox SDK v1 + PointPillar)
  cuda_add_executable(livox_live_mid70 tool/livox_live_mid70.cpp)
  add_dependencies(livox_live_mid70 pointpillar_core pointpillar_kernels)
  target_include_directories(livox_live_mid70 PRIVATE ${_LIVOX_INC})
  target_compile_definitions(livox_live_mid70 PRIVATE WITH_LIVOX=1)
  if(WITH_OPENCV)
    target_compile_definitions(livox_live_mid70 PRIVATE WITH_OPENCV=1)
  endif()
  target_link_libraries(livox_live_mid70
    -Wl,--whole-archive
      pointpillar_core
      pointpillar_kernels
    -Wl,--no-whole-archive
    nvinfer nvonnxparser nvinfer_plugin
    cudart cublas cublasLt curand
    Threads::Threads dl m
    ${_LIVOX_LIB}
    ${OpenCV_LIBS}
  )
  set_target_properties(livox_live_mid70 PROPERTIES
    BUILD_RPATH "/usr/lib/aarch64-linux-gnu;${CUDA_TOOLKIT_ROOT_DIR}/${CUDA_INSTALL_TARGET_DIR}/lib"
    INSTALL_RPATH "/usr/lib/aarch64-linux-gnu;${CUDA_TOOLKIT_ROOT_DIR}/${CUDA_INSTALL_TARGET_DIR}/lib"
  )
endif()

# =========================
# 실행파일: livox_live_mid360 (Livox SDK2 전용)
# =========================
if(WITH_LIVOX2)
  cuda_add_executable(livox_live_mid360
    tool/livox_live_mid360.cpp
  )

  # core/kernels 먼저
  add_dependencies(livox_live_mid360 pointpillar_core pointpillar_kernels)

  # include / defines
  target_include_directories(livox_live_mid360 PRIVATE ${_LIVOX2_INC})
  target_compile_definitions(livox_live_mid360 PRIVATE WITH_LIVOX2=1 WITH_LIVOX_SDK2=1)
  if(WITH_OPENCV)
    target_compile_definitions(livox_live_mid360 PRIVATE WITH_OPENCV=1)
  endif()

  # ★ plain 시그니처 한 번만 호출해서 링크
  target_link_libraries(livox_live_mid360
    -Wl,--whole-archive
      pointpillar_core
      pointpillar_kernels
    -Wl,--no-whole-archive
    nvinfer nvonnxparser nvinfer_plugin
    cudart cublas cublasLt curand
    Threads::Threads dl m
    ${_LIVOX2_LIB}
    ${OpenCV_LIBS}
  )

  set_target_properties(livox_live_mid360 PROPERTIES
    BUILD_RPATH "/usr/lib/aarch64-linux-gnu;${CUDA_TOOLKIT_ROOT_DIR}/${CUDA_INSTALL_TARGET_DIR}/lib"
    INSTALL_RPATH "/usr/lib/aarch64-linux-gnu;${CUDA_TOOLKIT_ROOT_DIR}/${CUDA_INSTALL_TARGET_DIR}/lib"
  )
else()
  message(STATUS "WITH_LIVOX2=OFF → livox_live_mid360 타깃은 만들지 않습니다.")
endif()


# =========================
# 실행파일: livox_live (실시간)
# =========================
cuda_add_executable(livox_live tool/livox_live.cpp)

# core/kernels 먼저 빌드되도록
add_dependencies(livox_live pointpillar_core pointpillar_kernels)

# 조건부 라이브러리/정의 모으기 (plain 시그니처 한 번에 링크)
set(_EXTRA_LIVOX_LIBS "")
if(WITH_LIVOX)
  list(APPEND _EXTRA_LIVOX_LIBS ${_LIVOX_LIB})
  target_include_directories(livox_live PRIVATE ${_LIVOX_INC})
  target_compile_definitions(livox_live PRIVATE WITH_LIVOX=1)
endif()
if(WITH_LIVOX2)
  list(APPEND _EXTRA_LIVOX_LIBS ${_LIVOX2_LIB})
  target_include_directories(livox_live PRIVATE ${_LIVOX2_INC})
  target_compile_definitions(livox_live PRIVATE WITH_LIVOX2=1 WITH_LIVOX_SDK2=1)
endif()
if(WITH_OPENCV)
  target_compile_definitions(livox_live PRIVATE WITH_OPENCV=1)
endif()

# plain 시그니처(한 번 호출)
target_link_libraries(livox_live
  -Wl,--whole-archive
    pointpillar_core
    pointpillar_kernels
  -Wl,--no-whole-archive
  nvinfer nvonnxparser nvinfer_plugin
  cudart cublas cublasLt curand
  Threads::Threads dl m
  ${_EXTRA_LIVOX_LIBS}
  ${OpenCV_LIBS}
)

set_target_properties(livox_live PROPERTIES
  BUILD_RPATH "/usr/lib/aarch64-linux-gnu;${CUDA_TOOLKIT_ROOT_DIR}/${CUDA_INSTALL_TARGET_DIR}/lib"
  INSTALL_RPATH "/usr/lib/aarch64-linux-gnu;${CUDA_TOOLKIT_ROOT_DIR}/${CUDA_INSTALL_TARGET_DIR}/lib"
)

# =========================
# 설정 요약
# =========================
message(STATUS "==== CONFIG SUMMARY ====")
message(STATUS "CMAKE_SYSTEM_PROCESSOR   : ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "CMAKE_BUILD_TYPE         : ${CMAKE_BUILD_TYPE}")
message(STATUS "WITH_LIVOX (SDK1)        : ${WITH_LIVOX}")
message(STATUS "WITH_LIVOX2 (SDK2)       : ${WITH_LIVOX2}")
message(STATUS "WITH_OPENCV              : ${WITH_OPENCV}")
message(STATUS "CUDA_TOOLKIT_ROOT_DIR    : ${CUDA_TOOLKIT_ROOT_DIR}")
message(STATUS "CUDA INCLUDE             : ${CUDA_INCLUDE_DIRS}")
message(STATUS "TensorRT include         : ${TENSORRT_INCLUDE_DIR}")
message(STATUS "TensorRT libs            : ${TENSORRT_LIB_INFER};${TENSORRT_LIB_ONNXPARSER};${TENSORRT_LIB_PLUGIN}")
if(WITH_OPENCV)
  message(STATUS "OpenCV libs              : ${OpenCV_LIBS}")
endif()
if(WITH_LIVOX)
  message(STATUS "Livox v1 include         : ${_LIVOX_INC}")
  message(STATUS "Livox v1 lib             : ${_LIVOX_LIB}")
endif()
if(WITH_LIVOX2)
  message(STATUS "Livox v2 include         : ${_LIVOX2_INC}")
  message(STATUS "Livox v2 lib             : ${_LIVOX2_LIB}")
endif()
message(STATUS "SM_LIST                  : ${SM_LIST}")
message(STATUS "=========================")
