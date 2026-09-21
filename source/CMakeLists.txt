add_library(scn_source STATIC
    SourceFactory.cpp
    detail/SyntheticSpectrumSource.cpp
    BB60CSource/BB60CSource.cpp
    HarogicSource/HarogicSource.cpp
    FileSource/FileSource.cpp
)

target_include_directories(scn_source PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(scn_source PUBLIC
    scn_algorithm
    scn_project_options
)
