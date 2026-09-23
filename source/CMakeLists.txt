add_library(scn_source STATIC
    SourceFactory.cpp
    SpectrumRecorder.cpp
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

# The hardware adapters use vendor APIs directly. Keep discovery here so the
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
    set_property(GLOBAL APPEND PROPERTY SCN_SOURCE_RUNTIME_DLLS
        "${SCN_BB60C_SDK_RUNTIME_DIR}/bb_api.dll"
        "${SCN_BB60C_SDK_RUNTIME_DIR}/ftd2xx.dll"
    )
    message(STATUS "BB60C SDK enabled: ${SCN_BB60C_SDK_ROOT}")
else()
    message(WARNING
        "BB60C SDK not found under ${SCN_BB60C_SDK_ROOT}; "
        "BB60C source will report a runtime configuration error.")
endif()

set(SCN_HAROGIC_SDK_INCLUDE_DIR "${SCN_HAROGIC_SDK_ROOT}/include")
set(SCN_HAROGIC_SDK_LIBRARY_DIR "${SCN_HAROGIC_SDK_ROOT}/lib")
set(SCN_HAROGIC_SDK_RUNTIME_DIR "${SCN_HAROGIC_SDK_ROOT}/bin")

find_path(SCN_HAROGIC_SDK_HEADER
    NAMES htra_api/htra_api.h
    PATHS "${SCN_HAROGIC_SDK_INCLUDE_DIR}"
    NO_DEFAULT_PATH
)
find_library(SCN_HAROGIC_SDK_LIBRARY
    NAMES htra_api
    PATHS "${SCN_HAROGIC_SDK_LIBRARY_DIR}"
    NO_DEFAULT_PATH
)

if(SCN_HAROGIC_SDK_HEADER AND SCN_HAROGIC_SDK_LIBRARY)
    target_compile_definitions(scn_source PRIVATE SCN_HAS_HAROGIC_SDK=1)
    target_include_directories(scn_source PRIVATE "${SCN_HAROGIC_SDK_INCLUDE_DIR}")
    target_link_libraries(scn_source PUBLIC "${SCN_HAROGIC_SDK_LIBRARY}")
    foreach(SCN_HAROGIC_RUNTIME_NAME IN ITEMS
        htra_api.dll
        libiomp5md.dll
        libliquid.dll
        libmkl.dll
        vcomp140.dll
        vcruntime140.dll)
        if(NOT EXISTS "${SCN_HAROGIC_SDK_RUNTIME_DIR}/${SCN_HAROGIC_RUNTIME_NAME}")
            message(FATAL_ERROR
                "Harogic HTRA runtime DLL is missing: "
                "${SCN_HAROGIC_SDK_RUNTIME_DIR}/${SCN_HAROGIC_RUNTIME_NAME}")
        endif()
        set_property(GLOBAL APPEND PROPERTY SCN_SOURCE_RUNTIME_DLLS
            "${SCN_HAROGIC_SDK_RUNTIME_DIR}/${SCN_HAROGIC_RUNTIME_NAME}")
    endforeach()
    message(STATUS "Harogic HTRA SDK enabled: ${SCN_HAROGIC_SDK_ROOT}")
else()
    message(WARNING
        "Harogic HTRA SDK not found under ${SCN_HAROGIC_SDK_ROOT}; "
        "Harogic source will report a runtime configuration error.")
endif()
