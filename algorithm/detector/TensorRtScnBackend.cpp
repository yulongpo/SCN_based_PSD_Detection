#include "IScnBackend.h"

#ifndef SCN_ENABLE_TENSORRT
#define SCN_ENABLE_TENSORRT 0
#endif

#if SCN_ENABLE_TENSORRT
#include "ScnSha256.h"

#include <NvInferRuntime.h>
#include <cuda_runtime_api.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <utility>

#if NV_TENSORRT_MAJOR != 10 || NV_TENSORRT_MINOR != 11
#error SCN requires the TensorRT 10.11 runtime API
#endif
#endif

namespace scn::algorithm
{
namespace
{
#if SCN_ENABLE_TENSORRT
constexpr std::size_t inputLength = 32768;
constexpr std::size_t outputLength = 8192;
constexpr std::size_t modelStride = 4;
static_assert(inputLength == outputLength * modelStride);
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);

// These are semantic bindings, never indices obtained from engine enumeration.
constexpr std::array<const char*, 4> tensorNames{{
    "resnet_32_input:0", "Identity:0", "Identity_1:0", "Identity_2:0"
}};

void checkCuda(cudaError_t status, const char* operation)
{
    if (status != cudaSuccess)
        throw std::runtime_error(std::string(operation) + ": " + cudaGetErrorName(status)
                                 + " (" + cudaGetErrorString(status) + ")");
}

// Destructors cannot return errors. Report failed cleanup without throwing or
// terminating the application, and keep attempting the remaining releases.
void reportCleanup(cudaError_t status, const char* operation) noexcept
{
    if (status != cudaSuccess)
        std::fprintf(stderr, "[SCN TensorRT] %s: %s (%s)\n", operation,
                     cudaGetErrorName(status), cudaGetErrorString(status));
}

class DeviceScope
{
public:
    explicit DeviceScope(int device)
    {
        checkCuda(cudaGetDevice(&previous_), "cudaGetDevice");
        if (device != previous_)
        {
            checkCuda(cudaSetDevice(device), "cudaSetDevice");
            changed_ = true;
        }
    }
    ~DeviceScope()
    {
        if (changed_)
            reportCleanup(cudaSetDevice(previous_), "restore CUDA device");
    }
    DeviceScope(const DeviceScope&) = delete;
    DeviceScope& operator=(const DeviceScope&) = delete;

private:
    int previous_ = -1;
    bool changed_ = false;
};

class TensorRtLogger final : public nvinfer1::ILogger
{
public:
    void log(Severity severity, const char* message) noexcept override
    {
        if (!message || severity > Severity::kWARNING)
            return;
        try
        {
            std::lock_guard<std::mutex> lock(mutex_);
            // Keep the latest bounded diagnostics, including deserialization errors.
            if (messages_.size() >= 8192)
                messages_.clear();
            messages_.append(message, (std::min)(std::strlen(message), std::size_t{4096}));
            messages_ += '\n';
        }
        catch (...)
        {
            std::fprintf(stderr, "[SCN TensorRT] %s\n", message);
        }
    }
    std::string messages() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return messages_;
    }

private:
    mutable std::mutex mutex_;
    std::string messages_;
};

// The entire partially initialized state is owned immediately. The logger
// outlives all TensorRT objects, and the engine outlives its execution context.
struct RuntimeState
{
    struct Buffer
    {
        void* device = nullptr;
        void* host = nullptr;
        std::size_t bytes = 0;
    };

    TensorRtLogger logger;
    std::unique_ptr<nvinfer1::IRuntime> runtime;
    std::unique_ptr<nvinfer1::ICudaEngine> engine;
    std::unique_ptr<nvinfer1::IExecutionContext> context;
    cudaStream_t stream = nullptr;
    std::array<Buffer, 4> buffers{};
    int deviceIndex = -1;

    ~RuntimeState() noexcept
    {
        int previous = -1;
        if (deviceIndex >= 0)
        {
            reportCleanup(cudaGetDevice(&previous), "cleanup cudaGetDevice");
            reportCleanup(cudaSetDevice(deviceIndex), "cleanup cudaSetDevice");
        }
        // Also executed after an enqueue/copy failure: pinned staging memory must
        // not be freed while a transfer or a TensorRT kernel may still use it.
        if (stream)
            reportCleanup(cudaStreamSynchronize(stream), "cleanup cudaStreamSynchronize");
        context.reset();
        for (auto& buffer : buffers)
        {
            if (buffer.device)
                reportCleanup(cudaFree(buffer.device), "cudaFree");
            if (buffer.host)
                reportCleanup(cudaFreeHost(buffer.host), "cudaFreeHost");
        }
        if (stream)
            reportCleanup(cudaStreamDestroy(stream), "cudaStreamDestroy");
        engine.reset();
        runtime.reset();
        if (previous >= 0 && previous != deviceIndex)
            reportCleanup(cudaSetDevice(previous), "cleanup restore CUDA device");
    }
};

std::string dimensions(const nvinfer1::Dims& dims)
{
    if (dims.nbDims < 0 || dims.nbDims > nvinfer1::Dims::MAX_DIMS)
        return "[invalid]";
    std::ostringstream text;
    text << '[';
    for (int i = 0; i < dims.nbDims; ++i)
        text << (i ? "," : "") << dims.d[i];
    text << ']';
    return text.str();
}

std::string dataType(nvinfer1::DataType type)
{
    switch (type)
    {
    case nvinfer1::DataType::kFLOAT: return "float32";
    case nvinfer1::DataType::kHALF: return "float16";
    case nvinfer1::DataType::kINT8: return "int8";
    case nvinfer1::DataType::kINT32: return "int32";
    case nvinfer1::DataType::kBOOL: return "bool";
    default: return "dtype(" + std::to_string(static_cast<int>(type)) + ')';
    }
}

std::vector<std::uint8_t> readEngine(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        throw std::runtime_error("Cannot open engine file: " + path.u8string());
    const auto end = file.tellg();
    if (end <= std::streampos{0})
        throw std::runtime_error("Engine file is empty or its size cannot be read");
    const auto size = static_cast<std::uintmax_t>(static_cast<std::streamoff>(end));
    if (size > static_cast<std::uintmax_t>((std::numeric_limits<std::streamsize>::max)())
        || size > std::vector<std::uint8_t>{}.max_size())
        throw std::runtime_error("Engine file is too large to read");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    file.seekg(0, std::ios::beg);
    if (!file || !file.read(reinterpret_cast<char*>(bytes.data()),
                            static_cast<std::streamsize>(bytes.size()))
        || file.gcount() != static_cast<std::streamsize>(bytes.size()))
        throw std::runtime_error("Incomplete engine read (I/O error or file changed while reading)");
    if (file.peek() != std::char_traits<char>::eof() || file.bad())
        throw std::runtime_error("Engine file changed while reading or an I/O error occurred");
    return bytes;
}

std::string cudaVersion(int version)
{
    return std::to_string(version / 1000) + '.' + std::to_string((version % 1000) / 10);
}

class TensorRtScnBackend final : public IScnBackend
{
public:
    bool initialize(const DetectorConfig& config, std::string& error) override
    {
        error.clear();
        state_.reset();
        info_ = "backend=TensorRT; status=initializing";
        try
        {
            if (config.inputLength != inputLength)
                throw std::runtime_error("SCN requires inputLength=32768, stride=4");
            if (config.deviceIndex < 0)
                throw std::runtime_error("GPU deviceIndex must be nonnegative");
            if (config.modelPath.empty())
                throw std::runtime_error("Engine modelPath is empty");
            const auto path = std::filesystem::absolute(
                std::filesystem::u8path(config.modelPath)).lexically_normal();
            info_ += "; engine=" + path.u8string();
            const auto bytes = readEngine(path);
            info_ += "; bytes=" + std::to_string(bytes.size())
                   + "; sha256=" + detail::sha256(bytes.data(), bytes.size());

            const auto trtVersion = getInferLibVersion();
            info_ += "; TensorRT.headers=" + std::to_string(NV_TENSORRT_VERSION)
                   + "; TensorRT.runtime=" + std::to_string(trtVersion);
            if (trtVersion != NV_TENSORRT_VERSION)
                throw std::runtime_error("TensorRT runtime/header version mismatch; deploy the matching SDK DLLs");
            int runtimeVersion = 0;
            int driverVersion = 0;
            checkCuda(cudaRuntimeGetVersion(&runtimeVersion), "cudaRuntimeGetVersion");
            checkCuda(cudaDriverGetVersion(&driverVersion), "cudaDriverGetVersion");
            info_ += "; CUDA.headers=" + cudaVersion(CUDART_VERSION)
                   + "; CUDA.runtime=" + cudaVersion(runtimeVersion)
                   + "; CUDA.driver=" + cudaVersion(driverVersion)
                   + "; GPU.index=" + std::to_string(config.deviceIndex);
            int deviceCount = 0;
            checkCuda(cudaGetDeviceCount(&deviceCount), "cudaGetDeviceCount");
            if (config.deviceIndex >= deviceCount)
                throw std::runtime_error("GPU deviceIndex=" + std::to_string(config.deviceIndex)
                                         + " is outside available device count=" + std::to_string(deviceCount));
            const DeviceScope device(config.deviceIndex);
            cudaDeviceProp properties{};
            checkCuda(cudaGetDeviceProperties(&properties, config.deviceIndex), "cudaGetDeviceProperties");
            info_ += "; GPU.name=" + std::string(properties.name)
                   + "; GPU.compute=" + std::to_string(properties.major) + '.' + std::to_string(properties.minor);

            state_ = std::make_unique<RuntimeState>();
            state_->deviceIndex = config.deviceIndex;
            auto& state = *state_;
            state.runtime.reset(nvinfer1::createInferRuntime(state.logger));
            if (!state.runtime)
                throw std::runtime_error("createInferRuntime failed");
            state.engine.reset(state.runtime->deserializeCudaEngine(bytes.data(), bytes.size()));
            if (!state.engine)
                throw std::runtime_error("deserializeCudaEngine failed: corrupt engine or incompatible GPU/runtime");
            validateEngine();
            checkCuda(cudaStreamCreateWithFlags(&state.stream, cudaStreamNonBlocking), "cudaStreamCreateWithFlags");
            state.context.reset(state.engine->createExecutionContext());
            if (!state.context)
                throw std::runtime_error("createExecutionContext failed");
            // Profile 0 is selected when TensorRT creates the context. Support a
            // dynamic batch declaration only when that profile accepts batch 1.
            const nvinfer1::Dims4 shape{1, static_cast<int>(inputLength), 1, 1};
            if (!state.context->setInputShape(tensorNames[0], shape))
                throw std::runtime_error("setInputShape rejected resnet_32_input:0 [1,32768,1,1]");
            validateContext();

            for (std::size_t i = 0; i < tensorNames.size(); ++i)
            {
                auto& buffer = state.buffers[i];
                buffer.bytes = (i == 0 ? inputLength : outputLength) * sizeof(float);
                checkCuda(cudaMalloc(&buffer.device, buffer.bytes), "cudaMalloc tensor buffer");
                checkCuda(cudaMallocHost(&buffer.host, buffer.bytes), "cudaMallocHost staging buffer");
                if (!state.context->setTensorAddress(tensorNames[i], buffer.device))
                    throw std::runtime_error("setTensorAddress failed for " + std::string(tensorNames[i]));
            }
            info_ += "; stride=4; status=ready";
            return true;
        }
        catch (const std::exception& exception)
        {
            return fail(exception.what(), error);
        }
        catch (...)
        {
            return fail("Unknown TensorRT initialization failure", error);
        }
    }

    bool infer(const std::vector<float>& normalized, ScnModelOutput& output,
               std::string& error) override
    {
        error.clear();
        output.heatmap.clear();
        output.bandwidth.clear();
        output.offset.clear();
        if (!state_)
        {
            error = "TensorRT SCN backend is unavailable or uninitialized; initialize successfully before infer";
            return false;
        }
        if (normalized.size() != inputLength)
        {
            error = "TensorRT SCN input must contain exactly 32768 normalized float32 values";
            return false;
        }
        if (!std::all_of(normalized.begin(), normalized.end(), [](float value) { return std::isfinite(value); }))
        {
            error = "TensorRT SCN input contains a non-finite value";
            return false;
        }
        try
        {
            auto& state = *state_;
            const DeviceScope device(state.deviceIndex);
            // Reuse caller capacity; all host/device transfer buffers and tensor
            // bindings remain fixed for the lifetime of the initialized backend.
            output.bandwidth.resize(outputLength);
            output.heatmap.resize(outputLength);
            output.offset.resize(outputLength);
            auto& input = state.buffers[0];
            std::memcpy(input.host, normalized.data(), input.bytes);
            checkCuda(cudaMemcpyAsync(input.device, input.host, input.bytes,
                                      cudaMemcpyHostToDevice, state.stream), "cudaMemcpyAsync input");
            if (!state.context->enqueueV3(state.stream))
                throw std::runtime_error("TensorRT enqueueV3 failed");
            for (std::size_t i = 1; i < state.buffers.size(); ++i)
            {
                const auto& buffer = state.buffers[i];
                checkCuda(cudaMemcpyAsync(buffer.host, buffer.device, buffer.bytes,
                                          cudaMemcpyDeviceToHost, state.stream), "cudaMemcpyAsync output");
            }
            checkCuda(cudaStreamSynchronize(state.stream), "cudaStreamSynchronize inference");
            const std::array<std::vector<float>*, 3> destinations{{
                &output.bandwidth, &output.heatmap, &output.offset
            }};
            for (std::size_t i = 0; i < destinations.size(); ++i)
            {
                const auto& buffer = state.buffers[i + 1];
                const auto* values = static_cast<const float*>(buffer.host);
                if (!std::all_of(values, values + outputLength, [](float value) { return std::isfinite(value); }))
                    throw std::runtime_error("Non-finite TensorRT output in " + std::string(tensorNames[i + 1]));
                std::memcpy(destinations[i]->data(), values, buffer.bytes);
            }
            return true;
        }
        catch (const std::exception& exception)
        {
            output.heatmap.clear();
            output.bandwidth.clear();
            output.offset.clear();
            return fail(exception.what(), error);
        }
        catch (...)
        {
            output.heatmap.clear();
            output.bandwidth.clear();
            output.offset.clear();
            return fail("Unknown TensorRT inference failure", error);
        }
    }

    std::string modelInfo() const override
    {
        if (!state_)
            return info_;
        const auto messages = state_->logger.messages();
        return messages.empty() ? info_ : info_ + "; TensorRT.messages=" + messages;
    }

private:
    void validateEngine()
    {
        const auto& engine = *state_->engine;
        const auto count = engine.getNbIOTensors();
        info_ += "; tensorCount=" + std::to_string(count);
        // Capture actual metadata before rejecting anything, so a mismatched
        // model is diagnosable without ever assigning tensors by ordinal.
        for (int i = 0; i < count; ++i)
        {
            const auto* name = engine.getIOTensorName(i);
            if (!name)
                throw std::runtime_error("TensorRT returned a null I/O tensor name");
            info_ += "; tensor=" + std::string(name)
                   + ",mode=" + std::to_string(static_cast<int>(engine.getTensorIOMode(name)))
                   + ",shape=" + dimensions(engine.getTensorShape(name))
                   + ",dtype=" + dataType(engine.getTensorDataType(name))
                   + ",format=" + std::to_string(static_cast<int>(engine.getTensorFormat(name, 0)))
                   + ",location=" + std::to_string(static_cast<int>(engine.getTensorLocation(name)));
        }
        if (count != static_cast<int>(tensorNames.size()))
            throw std::runtime_error("Unsupported engine: expected exactly one input and three named outputs");
        std::array<bool, 4> seen{};
        for (int i = 0; i < count; ++i)
        {
            const auto* name = engine.getIOTensorName(i);
            const auto found = std::find_if(tensorNames.begin(), tensorNames.end(),
                [name](const char* expected) { return std::strcmp(name, expected) == 0; });
            if (found == tensorNames.end())
                throw std::runtime_error("Unsupported I/O tensor name: " + std::string(name));
            const auto index = static_cast<std::size_t>(found - tensorNames.begin());
            if (seen[index])
                throw std::runtime_error("Duplicate I/O tensor name: " + std::string(name));
            seen[index] = true;
            const auto expectedMode = index == 0 ? nvinfer1::TensorIOMode::kINPUT : nvinfer1::TensorIOMode::kOUTPUT;
            if (engine.getTensorIOMode(name) != expectedMode
                || engine.getTensorDataType(name) != nvinfer1::DataType::kFLOAT
                || engine.getTensorLocation(name) != nvinfer1::TensorLocation::kDEVICE
                || engine.isShapeInferenceIO(name)
                || engine.getTensorFormat(name, 0) != nvinfer1::TensorFormat::kLINEAR
                || engine.getTensorVectorizedDim(name, 0) != -1)
                throw std::runtime_error("Unsupported tensor " + std::string(name)
                    + ": expected the declared input/output role and linear, device-resident float32 execution I/O"
                    + "; actual dtype=" + dataType(engine.getTensorDataType(name))
                    + ", role=" + std::to_string(static_cast<int>(engine.getTensorIOMode(name)))
                    + ", format=" + std::to_string(static_cast<int>(engine.getTensorFormat(name, 0)))
                    + ", location=" + std::to_string(static_cast<int>(engine.getTensorLocation(name))));
        }
        const auto shape = engine.getTensorShape(tensorNames[0]);
        if (shape.nbDims != 4 || (shape.d[0] != 1 && shape.d[0] != -1)
            || shape.d[1] != static_cast<std::int64_t>(inputLength) || shape.d[2] != 1 || shape.d[3] != 1)
            throw std::runtime_error("Unsupported input shape " + dimensions(shape)
                                     + ": expected [1,32768,1,1] (optional dynamic batch)");
        if (shape.d[0] == -1)
        {
            const auto minimum = engine.getProfileShape(tensorNames[0], 0, nvinfer1::OptProfileSelector::kMIN);
            const auto maximum = engine.getProfileShape(tensorNames[0], 0, nvinfer1::OptProfileSelector::kMAX);
            info_ += "; profile0.min=" + dimensions(minimum) + "; profile0.max=" + dimensions(maximum);
            if (minimum.nbDims != 4 || maximum.nbDims != 4 || minimum.d[0] != 1 || maximum.d[0] < 1)
                throw std::runtime_error("Profile 0 does not support batch 1");
        }
    }

    void validateContext()
    {
        const auto& context = *state_->context;
        for (std::size_t i = 0; i < tensorNames.size(); ++i)
        {
            const auto* name = tensorNames[i];
            const auto shape = context.getTensorShape(name);
            const auto strides = context.getTensorStrides(name);
            info_ += "; resolved=" + std::string(name) + dimensions(shape)
                   + ",strides=" + dimensions(strides);
            // Explicitly supported output layouts are [1,8192], [1,8192,1],
            // and [1,8192,1,1]. Merely having the same volume is insufficient.
            const auto length = static_cast<std::int64_t>(i == 0 ? inputLength : outputLength);
            if (shape.nbDims < 2 || shape.nbDims > 4 || (i == 0 && shape.nbDims != 4)
                || shape.d[0] != 1 || shape.d[1] != length)
                throw std::runtime_error("Unsupported resolved shape for " + std::string(name)
                    + ": " + dimensions(shape) + "; expected batch=1, frequency axis=" + std::to_string(length));
            for (int axis = 2; axis < shape.nbDims; ++axis)
                if (shape.d[axis] != 1)
                    throw std::runtime_error("Unsupported non-singleton trailing axis for " + std::string(name));
            if (strides.nbDims != shape.nbDims)
                throw std::runtime_error("Invalid tensor strides for " + std::string(name));
            std::int64_t contiguousStride = 1;
            for (int axis = shape.nbDims - 1; axis >= 0; --axis)
            {
                // Singleton axes do not affect the memory addresses accessed.
                if (shape.d[axis] > 1 && strides.d[axis] != contiguousStride)
                    throw std::runtime_error("Non-contiguous float32 tensor: " + std::string(name));
                contiguousStride *= shape.d[axis];
            }
        }
    }

    bool fail(const std::string& message, std::string& error)
    {
        // Invalidate immediately. If diagnostic formatting itself throws, this
        // local owner still synchronizes and releases the failed CUDA state.
        auto failedState = std::move(state_);
        error = message;
        if (failedState)
        {
            const auto diagnostics = failedState->logger.messages();
            if (!diagnostics.empty())
                error += "; TensorRT: " + diagnostics;
        }
        info_ += "; status=failed; error=" + error;
        return false;
    }

    std::unique_ptr<RuntimeState> state_;
    std::string info_ = "backend=TensorRT; status=uninitialized";
};
#else
class TensorRtScnBackend final : public IScnBackend
{
public:
    bool initialize(const DetectorConfig&, std::string& error) override
    {
        error = "TensorRT SCN backend unavailable: built with SCN_ENABLE_TENSORRT=OFF";
        return false;
    }
    bool infer(const std::vector<float>&, ScnModelOutput& output, std::string& error) override
    {
        output.heatmap.clear();
        output.bandwidth.clear();
        output.offset.clear();
        error = "TensorRT SCN inference unavailable: built with SCN_ENABLE_TENSORRT=OFF";
        return false;
    }
    std::string modelInfo() const override
    {
        return "backend=TensorRT; status=unavailable; SCN_ENABLE_TENSORRT=OFF";
    }
};
#endif
} // namespace

std::unique_ptr<IScnBackend> createTensorRtScnBackend()
{
    return std::make_unique<TensorRtScnBackend>();
}
} // namespace scn::algorithm
