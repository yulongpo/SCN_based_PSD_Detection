# cmake最低版本要求
cmake_minimum_required (VERSION 3.18)

#添加公共目录
include_directories(${SOLUTION_DIR}/src/com/common)
include_directories(${CMAKE_CURRENT_LIST_DIR}/common)

#添加子工程目录
add_subdirectory(HASM_SignalAlarm)
add_subdirectory(HASM_SignalRepo)
add_subdirectory(HASM_SignalWhitelist)
