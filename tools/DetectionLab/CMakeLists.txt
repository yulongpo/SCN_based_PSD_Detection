set(CMAKE_AUTOMOC ON)

add_executable(DetectionLab
    main.cpp
    LabIO.cpp
    LabSession.cpp
)

target_link_libraries(DetectionLab PRIVATE
    scn_application
    scn_algorithm
    scn_source
    scn_project_options
    Qt6::Core
)
scn_deploy_detection_runtime(DetectionLab)
if(WIN32)
    add_custom_command(TARGET DetectionLab POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:Qt6::Core>" "$<TARGET_FILE_DIR:DetectionLab>"
        COMMENT "Deploying Qt Core for DetectionLab" VERBATIM)
endif()

add_executable(DetectionLabUI WIN32
    main_ui.cpp
    DetectionLabWorker.h
    DetectionLabWindow.h
    ../../app/HaiAISpecMonitor/ui/DetectionConfigWidget.h
    ../../app/HaiAISpecMonitor/ui/SpectrumWidget.h
    ../../app/HaiAISpecMonitor/ui/SpectrumRenderWorker.h
    LabIO.cpp
    LabSession.cpp
    DetectionLabWorker.cpp
    DetectionLabWindow.cpp
    ../../app/HaiAISpecMonitor/ui/DetectionConfigWidget.cpp
    ../../app/HaiAISpecMonitor/ui/SpectrumWidget.cpp
    ../../app/HaiAISpecMonitor/ui/SpectrumRenderWorker.cpp
    ../../app/HaiAISpecMonitor/ui/Direct2DChartRenderer.cpp
)

target_include_directories(DetectionLabUI PRIVATE
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/app/HaiAISpecMonitor/ui
)
target_link_libraries(DetectionLabUI PRIVATE
    scn_application
    scn_algorithm
    scn_source
    scn_project_options
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

if(WIN32)
    target_link_libraries(DetectionLabUI PRIVATE d2d1 dwrite)
    find_program(SCN_WINDEPLOYQT_EXECUTABLE
        NAMES windeployqt.exe
        HINTS
            "${CMAKE_PREFIX_PATH}/bin"
            "${Qt6_DIR}/../../../bin"
            "${Qt6Core_DIR}/../../../bin"
    )
    if(SCN_WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET DetectionLabUI POST_BUILD
            COMMAND "${SCN_WINDEPLOYQT_EXECUTABLE}"
                $<IF:$<CONFIG:Debug>,--debug,--release>
                --no-translations
                --no-system-d3d-compiler
                --no-quick-import
                --compiler-runtime
                --dir "$<TARGET_FILE_DIR:DetectionLabUI>"
                "$<TARGET_FILE:DetectionLabUI>"
            WORKING_DIRECTORY "$<TARGET_FILE_DIR:DetectionLabUI>"
            COMMENT "Deploying Qt runtime for DetectionLabUI"
            VERBATIM
        )
    else()
        message(WARNING "windeployqt.exe was not found; DetectionLabUI Qt deployment is disabled")
    endif()
endif()
scn_deploy_detection_runtime(DetectionLabUI)
