add_library(scn_algorithm STATIC
    DetectionConfig.cpp
    DetectionEngine/DetectionEngine.cpp
)

target_include_directories(scn_algorithm PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(scn_algorithm PUBLIC scn_project_options)
