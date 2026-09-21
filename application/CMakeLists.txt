set(CMAKE_AUTOMOC ON)

add_library(scn_application STATIC
    MonitoringSession.cpp
    SourceManager.cpp
    PresentationModel.cpp
)

target_include_directories(scn_application PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(scn_application PUBLIC
    scn_algorithm
    scn_runtime
    scn_source
    scn_project_options
    Qt6::Core
)
