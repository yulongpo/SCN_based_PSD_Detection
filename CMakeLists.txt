cmake_minimum_required(VERSION 3.28)

project(SCN_Based_PSD_Detection
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SCN_BUILD_TESTS "Configure the user-owned test targets" ON)
option(SCN_BUILD_DETECTION_LAB "Build the standalone DetectionLab tool" ON)

set(SCN_BB60C_SDK_ROOT
    "D:/project/isa2/3rdparty/bb_series"
    CACHE PATH "Signal Hound BB API SDK root")

find_package(Qt6 6.11 REQUIRED COMPONENTS Core Gui Widgets)

add_library(scn_project_options INTERFACE)
target_compile_features(scn_project_options INTERFACE cxx_std_17)
target_include_directories(scn_project_options INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
if(MSVC)
    target_compile_options(scn_project_options INTERFACE /utf-8)
endif()

add_subdirectory(algorithm)
add_subdirectory(runtime)
add_subdirectory(source)
add_subdirectory(application)
add_subdirectory(app/HaiAISpecMonitor)

if(SCN_BUILD_DETECTION_LAB)
    add_subdirectory(tools/DetectionLab)
endif()

if(SCN_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
