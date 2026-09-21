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

# The BB60C adapter uses the vendor API directly.  Keep discovery here so the
# rest of the application only depends on the stable ISpectrumSource contract.
set(SCN_BB60C_SDK_INCLUDE_DIR "${SCN_BB60C_SDK_ROOT}/include")
set(SCN_BB60C_SDK_LIBRARY_DIR "${SCN_BB60C_SDK_ROOT}/lib")
set(SCN_BB60C_SDK_RUNTIME_DIR "${SCN_BB60C_SDK_ROOT}/bin")

find_path(SCN_BB60C_SDK_HEADER
    NAMES bb_series/bb_api.h
    PATHS "${SCN_BB60C_SDK_INCLUDE_DIR}"
    NO_DEFAULT_PATH
)
find_library(SCN_BB60C_SDK_LIBRARY
    NAMES bb_api
    PATHS "${SCN_BB60C_SDK_LIBRARY_DIR}"
    NO_DEFAULT_PATH
)

if(SCN_BB60C_SDK_HEADER AND SCN_BB60C_SDK_LIBRARY)
    target_compile_definitions(scn_source PRIVATE SCN_HAS_BB60C_SDK=1)
    target_include_directories(scn_source PRIVATE "${SCN_BB60C_SDK_INCLUDE_DIR}")
    target_link_libraries(scn_source PUBLIC "${SCN_BB60C_SDK_LIBRARY}")
    set_property(GLOBAL PROPERTY SCN_BB60C_RUNTIME_DLLS
        "${SCN_BB60C_SDK_RUNTIME_DIR}/bb_api.dll"
        "${SCN_BB60C_SDK_RUNTIME_DIR}/ftd2xx.dll"
    )
    message(STATUS "BB60C SDK enabled: ${SCN_BB60C_SDK_ROOT}")
else()
    message(WARNING
        "BB60C SDK not found under ${SCN_BB60C_SDK_ROOT}; "
        "BB60C source will report a runtime configuration error.")
endif()
