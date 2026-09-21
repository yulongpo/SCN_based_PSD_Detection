add_executable(DetectionLab
    main.cpp
    LabIO.cpp
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
