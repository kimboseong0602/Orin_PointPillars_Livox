#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "jetson-utils" for configuration "Release"
set_property(TARGET jetson-utils APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(jetson-utils PROPERTIES
  IMPORTED_LINK_INTERFACE_LIBRARIES_RELEASE "/usr/local/cuda/lib64/libcudart_static.a;-lpthread;dl;/usr/lib/aarch64-linux-gnu/librt.so;GL;GLU;GLEW;gstreamer-1.0;gstapp-1.0;gstpbutils-1.0;gstwebrtc-1.0;gstsdp-1.0;gstrtspserver-1.0;json-glib-1.0;soup-2.4;/usr/local/cuda/lib64/libnppicc.so;nvbuf_utils"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libjetson-utils.so"
  IMPORTED_SONAME_RELEASE "libjetson-utils.so"
  )

list(APPEND _IMPORT_CHECK_TARGETS jetson-utils )
list(APPEND _IMPORT_CHECK_FILES_FOR_jetson-utils "${_IMPORT_PREFIX}/lib/libjetson-utils.so" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
