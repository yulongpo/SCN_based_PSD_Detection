add_executable(DetectionLab
    main.cpp
)

target_link_libraries(DetectionLab PRIVATE
    scn_application
    scn_algorithm
    scn_source
    scn_project_options
    Qt6::Core
)
