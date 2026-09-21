cmake_minimum_required(VERSION 3.28)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

file(GLOB APP_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/ui/*.h"
)
file(GLOB APP_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/ui/*.cpp"
)
file(GLOB CONTROLLER_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/controller/*.h"
)
file(GLOB CONTROLLER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/controller/*.cpp"
)
file(GLOB VIEWMODEL_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/viewmodel/*.h"
)
file(GLOB VIEWMODEL_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/viewmodel/*.cpp"
)

add_executable(HaiAISpecMonitor
    main.cpp
    ui/ui_resources.qrc
    ${APP_HEADERS}
    ${APP_SOURCES}
    ${CONTROLLER_HEADERS}
    ${CONTROLLER_SOURCES}
    ${VIEWMODEL_HEADERS}
    ${VIEWMODEL_SOURCES}
)

target_include_directories(HaiAISpecMonitor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/ui
    ${PROJECT_SOURCE_DIR}
)
target_link_libraries(HaiAISpecMonitor PRIVATE
    scn_application
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

set_target_properties(HaiAISpecMonitor PROPERTIES
    WIN32_EXECUTABLE TRUE
    DEBUG_POSTFIX "d"
)

# Qt DLLs are not copied automatically by a Visual Studio CMake target.
# Deploy the matching Debug/Release Qt runtime beside the executable so
# starting the target from VS does not depend on the global PATH.
if(WIN32)
    find_program(SCN_WINDEPLOYQT_EXECUTABLE
        NAMES windeployqt.exe
        HINTS
            "${CMAKE_PREFIX_PATH}/bin"
            "${Qt6_DIR}/../../../bin"
            "${Qt6Core_DIR}/../../../bin"
    )

    if(SCN_WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET HaiAISpecMonitor POST_BUILD
            COMMAND "${SCN_WINDEPLOYQT_EXECUTABLE}"
                $<IF:$<CONFIG:Debug>,--debug,--release>
                --no-translations
                --no-system-d3d-compiler
                --compiler-runtime
                --dir "$<TARGET_FILE_DIR:HaiAISpecMonitor>"
                "$<TARGET_FILE:HaiAISpecMonitor>"
            WORKING_DIRECTORY "$<TARGET_FILE_DIR:HaiAISpecMonitor>"
            COMMENT "Deploying Qt runtime for HaiAISpecMonitor"
            VERBATIM
        )
    else()
        message(WARNING "windeployqt.exe was not found; Qt DLL deployment is disabled")
    endif()
endif()

install(TARGETS HaiAISpecMonitor
    RUNTIME DESTINATION bin
)
