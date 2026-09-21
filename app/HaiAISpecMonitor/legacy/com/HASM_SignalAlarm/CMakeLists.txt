cmake_minimum_required(VERSION 3.18)

project(HASM_SignalAlarm)

set(PROJECT_DIR  ${PROJECT_NAME})
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_definitions(-DRAICOM_EXPORTS)

file(GLOB_RECURSE SOURCE
    ${CMAKE_CURRENT_LIST_DIR}/src/*.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/*.c
    ${CMAKE_CURRENT_LIST_DIR}/src/*.cc
)

add_library(${PROJECT_NAME} SHARED ${SOURCE})
radioai_configure_component_output(${PROJECT_NAME})

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src)
target_include_directories(${PROJECT_NAME} PUBLIC ${SOLUTION_DIR}/src/core/include)
