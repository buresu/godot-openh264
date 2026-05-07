cmake_minimum_required(VERSION 3.15)

project(godot-openh264 LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(CMAKE_GENERATOR_PLATFORM MATCHES "arm64|aarch64|ARM64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64|ARM64")
    set(ARCH "arm64")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(ARCH "x86_64")
else()
    set(ARCH "x86_32")
endif()

# godot-cpp
add_subdirectory("${PROJECT_SOURCE_DIR}/lib/godot-cpp")

# bzib2
set(ENABLE_LIB_ONLY   ON  CACHE BOOL "" FORCE)
set(ENABLE_STATIC_LIB ON  CACHE BOOL "" FORCE)
set(ENABLE_SHARED_LIB OFF CACHE BOOL "" FORCE)
add_subdirectory("${PROJECT_SOURCE_DIR}/lib/bzip2" EXCLUDE_FROM_ALL)

# libyuv
add_subdirectory("${PROJECT_SOURCE_DIR}/lib/libyuv" EXCLUDE_FROM_ALL)

file(GLOB_RECURSE INCLUDES "src/*.hpp")
file(GLOB_RECURSE SOURCES "src/*.cpp")

add_library(GodotOpenH264 SHARED ${SOURCES} ${INCLUDES})

set_target_properties(GodotOpenH264 PROPERTIES
    PREFIX "lib"
    OUTPUT_NAME "godot-openh264.$<IF:$<PLATFORM_ID:Windows>,windows,$<IF:$<PLATFORM_ID:Darwin>,macos,linux>>.$<IF:$<CONFIG:Debug>,debug,release>.${ARCH}"
)

target_include_directories(GodotOpenH264 PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/lib/godot-cpp/gdextension
    ${PROJECT_SOURCE_DIR}/lib/godot-cpp/include
    ${PROJECT_SOURCE_DIR}/lib/godot-cpp/gen/include
    ${PROJECT_SOURCE_DIR}/lib/openh264/codec/api/wels
    ${PROJECT_SOURCE_DIR}/lib/minimp4
    ${PROJECT_SOURCE_DIR}/lib/libyuv/include
)

target_link_libraries(GodotOpenH264 PRIVATE godot-cpp bz2_static yuv)
if(WIN32)
    target_link_libraries(GodotOpenH264 PRIVATE kernel32)
endif()

set_target_properties(GodotOpenH264  PROPERTIES
    SKIP_RPATH TRUE
    BUILD_RPATH_USE_ORIGIN TRUE
    SKIP_BUILD_RPATH FALSE
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH_USE_LINK_PATH FALSE)
if(APPLE)
    set_target_properties(GodotOpenH264 PROPERTIES INSTALL_RPATH "@loader_path")
elseif(UNIX)
    set_target_properties(GodotOpenH264 PROPERTIES INSTALL_RPATH "$ORIGIN")
endif()

install(TARGETS GodotOpenH264
    RUNTIME DESTINATION "${PROJECT_SOURCE_DIR}/demo/addons/godot-openh264/bin"
    LIBRARY DESTINATION "${PROJECT_SOURCE_DIR}/demo/addons/godot-openh264/bin"
)
