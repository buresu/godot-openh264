cmake_minimum_required(VERSION 3.15)

project(godot-openh264 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ---- Architecture detection ----
if(NOT DEFINED GODOT_ARCH)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
        set(GODOT_ARCH "arm64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
        set(GODOT_ARCH "x86_64")
    else()
        set(GODOT_ARCH "x86_32")
    endif()
endif()

# ---- Build type suffix ----
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(GODOT_CONFIG "debug")
else()
    set(GODOT_CONFIG "release")
endif()

# ---- Platform name ----
if(WIN32)
    set(GODOT_PLATFORM "windows")
    set(LIB_SUFFIX ".dll")
elseif(APPLE)
    set(GODOT_PLATFORM "macos")
    set(LIB_SUFFIX ".dylib")
else()
    set(GODOT_PLATFORM "linux")
    set(LIB_SUFFIX ".so")
endif()

set(OUTPUT_NAME "libgodot-openh264.${GODOT_PLATFORM}.${GODOT_CONFIG}.${GODOT_ARCH}${LIB_SUFFIX}")

# ---- godot-cpp ----
set(GODOT_CPP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib/godot-cpp")
if(NOT EXISTS "${GODOT_CPP_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "godot-cpp not found. Run: git submodule update --init --recursive")
endif()
add_subdirectory(${GODOT_CPP_DIR})

# ---- openh264 (headers only) ----
set(OPENH264_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib/openh264/codec/api/wels")

# ---- bzip2 (built from submodule) ----
set(BZIP2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib/bzip2")
if(NOT EXISTS "${BZIP2_DIR}/bzlib.c")
    message(FATAL_ERROR "bzip2 not found. Run: git submodule update --init --recursive")
endif()

set(ENABLE_LIB_ONLY   ON  CACHE BOOL "" FORCE)
set(ENABLE_STATIC_LIB ON  CACHE BOOL "" FORCE)
set(ENABLE_SHARED_LIB OFF CACHE BOOL "" FORCE)
add_subdirectory(${BZIP2_DIR})

# ---- libyuv (built from submodule) ----
set(LIBYUV_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib/libyuv")
if(NOT EXISTS "${LIBYUV_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "libyuv not found. Run: git submodule update --init --recursive")
endif()
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(${LIBYUV_DIR})

# ---- Source files ----
add_library(godot-openh264 SHARED
    src/register_types.cpp
    src/OpenH264Loader.cpp
    src/OpenH264Decoder.cpp
    src/OpenH264VideoStream.cpp
    src/OpenH264VideoStreamPlayback.cpp
)

target_include_directories(godot-openh264 PRIVATE
    src/
    ${OPENH264_INCLUDE_DIR}
    lib/minimp4/
    lib/libyuv/include/
)

target_link_libraries(godot-openh264 PRIVATE godot-cpp bz2_static yuv)

if(WIN32)
    # LoadLibrary / GetProcAddress
    target_link_libraries(godot-openh264 PRIVATE kernel32)
endif()

# ---- Output ----
set_target_properties(godot-openh264 PROPERTIES
    OUTPUT_NAME "godot-openh264.${GODOT_PLATFORM}.${GODOT_CONFIG}.${GODOT_ARCH}"
    PREFIX "lib"
    SUFFIX "${LIB_SUFFIX}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/demo/addons/godot-openh264/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/demo/addons/godot-openh264/bin"
)
