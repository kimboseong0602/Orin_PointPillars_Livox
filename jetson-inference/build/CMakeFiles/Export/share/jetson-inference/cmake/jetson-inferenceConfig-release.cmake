#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "jetson-inference" for configuration "Release"
set_property(TARGET jetson-inference APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(jetson-inference PROPERTIES
  IMPORTED_LINK_INTERFACE_LIBRARIES_RELEASE "/usr/local/cuda/lib64/libcudart_static.a;-lpthread;dl;/usr/lib/aarch64-linux-gnu/librt.so;jetson-utils;nvinfer;nvinfer_plugin;nvparsers;nvonnxparser;opencv_core;opencv_calib3d;vpi"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libjetson-inference.so"
  IMPORTED_SONAME_RELEASE "libjetson-inference.so"
  )

list(APPEND _IMPORT_CHECK_TARGETS jetson-inference )
list(APPEND _IMPORT_CHECK_FILES_FOR_jetson-inference "${_IMPORT_PREFIX}/lib/libjetson-inference.so" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
