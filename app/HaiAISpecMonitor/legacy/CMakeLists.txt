#设置cmake版本号
cmake_minimum_required(VERSION 3.18)
project(HaiAISpecMonitor LANGUAGES CXX)

# 2. C++标准设置（允许子项目覆盖）
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)  # 禁用编译器扩展

#添加业务组件
add_subdirectory(com)

# 自动处理UI、MOC和资源文件
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOMOC_PATH_PREFIX ON)
set(CMAKE_AUTORCC ON)

find_package(Qt5 5.12 REQUIRED COMPONENTS Core Gui Widgets Svg PrintSupport)


# 递归查找src目录下的所有源文件
file(GLOB_RECURSE SOURCES
        "src/*.cpp"
        "src/*.cc"
        "src/*.cxx"
)

# 递归查找src目录下的所有头文件
file(GLOB_RECURSE HEADERS
        "src/*.h"
        "src/*.hpp"
        "src/*.hxx"
)

# 递归查找UI文件
file(GLOB_RECURSE FORMS
        "src/*.ui"
)

# 递归查找资源文件
file(GLOB_RECURSE RESOURCES
        "src/*.qrc"
        "src/*.rc"
)

# 创建可执行文件或库
add_executable(${PROJECT_NAME}
        ${SOURCES}
        ${HEADERS}
        ${FORMS}
        ${RESOURCES}
)
radioai_configure_target_output(${PROJECT_NAME})
radioai_configure_hai_application(${PROJECT_NAME})

# 然后设置属性关闭控制台
set_target_properties(${PROJECT_NAME} PROPERTIES
        WIN32_EXECUTABLE FALSE
)


# 设置包含目录
target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${SOLUTION_DIR}/3rdparty/haibase
        ${SOLUTION_DIR}/src/core/include
)

target_link_directories(${PROJECT_NAME} PRIVATE

)

# 链接Qt库
target_link_libraries(${PROJECT_NAME} PRIVATE
        Qt5::Core
        Qt5::Gui
        Qt5::Widgets
        Qt5::Svg
        Qt5::PrintSupport
        RadioAICore
        dwmapi
)

set(CMAKE_BUILD_RPATH "$ORIGIN")
set(CMAKE_INSTALL_RPATH "$ORIGIN")

# 安装
install(TARGETS ${PROJECT_NAME}
        RUNTIME DESTINATION HaiAISpecMonitor/
        COMPONENT HaiAISpecMonitor
)
radioai_add_dev_runtime_target(${PROJECT_NAME})
radioai_add_dev_run_target(${PROJECT_NAME})
