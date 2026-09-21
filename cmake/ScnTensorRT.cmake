include_guard(GLOBAL)

if(WIN32)
    set(_scn_tensorrt_default ON)
else()
    set(_scn_tensorrt_default OFF)
endif()
option(SCN_ENABLE_TENSORRT "Enable the TensorRT SCN inference backend" ${_scn_tensorrt_default})
unset(_scn_tensorrt_default)

set(SCN_TENSORRT_ROOT
    "${PROJECT_SOURCE_DIR}/third_party/tensorrt"
    CACHE PATH "TensorRT 10.11 SDK root (include, lib, and matching bin)")
set(SCN_MODEL_SOURCE
    "${PROJECT_SOURCE_DIR}/models/scn_model.engine"
    CACHE FILEPATH "Serialized SCN engine copied to models/scn_model.engine")

if(SCN_ENABLE_TENSORRT)
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "The SCN TensorRT backend requires a 64-bit target")
    endif()

    # Runtime API only: CXX stays the sole project language; nvcc is never used
    # to compile this backend. CUDAToolkit supplies headers and the import library.
    # A plain VS CMake invocation need not export PROCESSOR_ARCHITECTURE or put
    # cl.exe on PATH. FindCUDAToolkit can then miss lib/x64 despite an x64 target.
    # Seed its runtime search from the explicitly selected toolkit, not the host.
    if(WIN32 AND CUDAToolkit_ROOT)
        find_library(_scn_cuda_import_library NAMES cudart
            PATHS "${CUDAToolkit_ROOT}/lib/x64" NO_DEFAULT_PATH NO_CACHE REQUIRED)
        set(CUDA_CUDART "${_scn_cuda_import_library}")
        set(CUDA_cudart_LIBRARY "${_scn_cuda_import_library}")
    endif()
    find_package(CUDAToolkit 12 REQUIRED)
    if(NOT CUDAToolkit_VERSION VERSION_LESS 13)
        message(FATAL_ERROR "SCN requires CUDA 12 headers/import libraries; set CUDAToolkit_ROOT")
    endif()
    find_path(SCN_TENSORRT_INCLUDE_DIR
        NAMES NvInferRuntime.h
        PATHS "${SCN_TENSORRT_ROOT}/include"
        NO_DEFAULT_PATH NO_CACHE REQUIRED)
    find_library(SCN_TENSORRT_LIBRARY
        NAMES nvinfer nvinfer_10
        PATHS "${SCN_TENSORRT_ROOT}/lib" "${SCN_TENSORRT_ROOT}/lib64"
        NO_DEFAULT_PATH NO_CACHE REQUIRED)

    # 10.11 headers express NV_TENSORRT_* through TRT_*_ENTERPRISE macros.
    # Accept either spelling, but never pick another SDK from a global PATH.
    file(STRINGS "${SCN_TENSORRT_INCLUDE_DIR}/NvInferVersion.h" _scn_version_lines
        REGEX "^#define[ \t]+(NV_TENSORRT|TRT)_(MAJOR|MINOR|PATCH)(_ENTERPRISE)?[ \t]+[0-9]+")
    foreach(_scn_line IN LISTS _scn_version_lines)
        if(_scn_line MATCHES "^#define[ \t]+(NV_TENSORRT|TRT)_(MAJOR|MINOR|PATCH)(_ENTERPRISE)?[ \t]+([0-9]+)")
            set(_scn_trt_${CMAKE_MATCH_2} "${CMAKE_MATCH_4}")
        endif()
    endforeach()
    if(NOT "${_scn_trt_MAJOR}.${_scn_trt_MINOR}.${_scn_trt_PATCH}" STREQUAL "10.11.0")
        message(FATAL_ERROR "SCN requires TensorRT 10.11.0; check SCN_TENSORRT_ROOT")
    endif()

    if(WIN32)
        # The 10.11 SDK bundles CUDA 12.9 DLLs. Do not deploy CUDA 12.4's
        # identically named cudart DLL, nor glob the toolkit's runtime directory.
        # cuBLAS/cuBLASLt are loaded by TensorRT tactics at runtime and therefore
        # are not reliably discovered through PE import-table scanning alone.
        set(_scn_runtime_names
            nvinfer_10.dll
            cudart64_12.dll
            cublas64_12.dll
            cublasLt64_12.dll)
        set(_scn_runtime_dlls)
        foreach(_scn_name IN LISTS _scn_runtime_names)
            set(_scn_path "${SCN_TENSORRT_ROOT}/bin/${_scn_name}")
            if(NOT EXISTS "${_scn_path}" OR IS_DIRECTORY "${_scn_path}")
                message(FATAL_ERROR "Missing matching TensorRT runtime DLL: ${_scn_path}")
            endif()
            list(APPEND _scn_runtime_dlls "${_scn_path}")
        endforeach()
        set_property(GLOBAL PROPERTY SCN_DETECTION_RUNTIME_DLLS "${_scn_runtime_dlls}")

        add_library(SCN::TensorRT SHARED IMPORTED GLOBAL)
        set_target_properties(SCN::TensorRT PROPERTIES
            IMPORTED_IMPLIB "${SCN_TENSORRT_LIBRARY}"
            IMPORTED_LOCATION "${SCN_TENSORRT_ROOT}/bin/nvinfer_10.dll"
            INTERFACE_INCLUDE_DIRECTORIES "${SCN_TENSORRT_INCLUDE_DIR}")

        # Use a separate imported target rather than mutating CUDA::cudart.
        # TARGET_RUNTIME_DLLS consumers also see the SDK's 12.9 runtime here.
        add_library(SCN::CudaRuntime SHARED IMPORTED GLOBAL)
        set_target_properties(SCN::CudaRuntime PROPERTIES
            IMPORTED_IMPLIB "${CUDA_cudart_LIBRARY}"
            IMPORTED_LOCATION "${SCN_TENSORRT_ROOT}/bin/cudart64_12.dll"
            INTERFACE_INCLUDE_DIRECTORIES "${CUDAToolkit_INCLUDE_DIRS}")
    else()
        add_library(SCN::TensorRT UNKNOWN IMPORTED GLOBAL)
        set_target_properties(SCN::TensorRT PROPERTIES
            IMPORTED_LOCATION "${SCN_TENSORRT_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${SCN_TENSORRT_INCLUDE_DIR}")
    endif()
    message(STATUS "SCN TensorRT: ${SCN_TENSORRT_ROOT}; CUDA headers: ${CUDAToolkit_VERSION}")
endif()

# Call once after adding detector/TensorRtScnBackend.cpp to scn_algorithm.
# Keep that source in the target even when disabled, for the unavailable backend.
# SDK types and compile definitions remain private to the target.
function(scn_configure_tensorrt_backend target)
    if(NOT TARGET "${target}")
        message(FATAL_ERROR "scn_configure_tensorrt_backend: unknown target ${target}")
    endif()
    get_target_property(_scn_configured "${target}" SCN_TENSORRT_CONFIGURED)
    if(_scn_configured)
        return()
    endif()
    target_compile_features("${target}" PRIVATE cxx_std_17)
    target_compile_definitions("${target}" PRIVATE
        SCN_ENABLE_TENSORRT=$<BOOL:${SCN_ENABLE_TENSORRT}>)
    if(SCN_ENABLE_TENSORRT)
        target_link_libraries("${target}" PRIVATE SCN::TensorRT)
        if(WIN32)
            target_link_libraries("${target}" PRIVATE SCN::CudaRuntime)
        else()
            target_link_libraries("${target}" PRIVATE CUDA::cudart)
        endif()
    endif()
    set_property(TARGET "${target}" PROPERTY SCN_TENSORRT_CONFIGURED TRUE)
endfunction()

# Call in the same CMakeLists.txt that creates the executable, after its other
# POST_BUILD deployment steps. No parser/builder/plugin/trtexec or legacy Windows
# API-set shim is needed for this native-layer SCN engine. Additional/custom
# plugins are intentionally unsupported rather than guessed or auto-loaded.
function(scn_deploy_detection_runtime target)
    if(NOT TARGET "${target}")
        message(FATAL_ERROR "scn_deploy_detection_runtime: unknown target ${target}")
    endif()
    if(NOT SCN_ENABLE_TENSORRT)
        return()
    endif()
    get_target_property(_scn_deployed "${target}" SCN_DETECTION_RUNTIME_DEPLOYED)
    if(_scn_deployed)
        return()
    endif()
    get_filename_component(_scn_model "${SCN_MODEL_SOURCE}" ABSOLUTE BASE_DIR "${PROJECT_SOURCE_DIR}")
    if(NOT EXISTS "${_scn_model}" OR IS_DIRECTORY "${_scn_model}")
        message(FATAL_ERROR "SCN engine missing: ${_scn_model}; set SCN_MODEL_SOURCE")
    endif()
    add_custom_command(TARGET "${target}" POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:${target}>/models"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${_scn_model}" "$<TARGET_FILE_DIR:${target}>/models/scn_model.engine"
        COMMENT "Deploying SCN TensorRT engine for ${target}"
        VERBATIM)
    install(FILES "${_scn_model}" DESTINATION bin/models RENAME scn_model.engine
        COMPONENT SCNDetectionRuntime)
    if(WIN32)
        get_property(_scn_runtime_dlls GLOBAL PROPERTY SCN_DETECTION_RUNTIME_DLLS)
        add_custom_command(TARGET "${target}" POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                ${_scn_runtime_dlls} "$<TARGET_FILE_DIR:${target}>"
            COMMENT "Deploying matching TensorRT / CUDA runtime from TensorRT SDK bin"
            COMMAND_EXPAND_LISTS VERBATIM)
        install(FILES ${_scn_runtime_dlls} DESTINATION bin COMPONENT SCNDetectionRuntime)
    endif()
    set_property(TARGET "${target}" PROPERTY SCN_DETECTION_RUNTIME_DEPLOYED TRUE)
endfunction()
