add_library(scn_algorithm STATIC
    DetectionConfig.cpp
    DetectionEngine/DetectionEngine.cpp
    preprocess/SpectrumPreprocessor.cpp
    accumulation/TemporalAccumulator.cpp
    aggregation/ChannelAggregator.cpp
    detector/ScnDecoder.cpp
    detector/TensorRtScnBackend.cpp
    refine/CnrRefiner.cpp
    fusion/SignalFusion.cpp
    tracking/SignalTracker.cpp
)

target_include_directories(scn_algorithm PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(scn_algorithm PUBLIC scn_project_options)
scn_configure_tensorrt_backend(scn_algorithm)
