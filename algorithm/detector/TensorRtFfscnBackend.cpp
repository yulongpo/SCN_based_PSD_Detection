#include "IFfscnBackend.h"

#ifndef SCN_ENABLE_TENSORRT
#define SCN_ENABLE_TENSORRT 0
#endif

#if SCN_ENABLE_TENSORRT
#include "ScnSha256.h"
#include <NvInferRuntime.h>
#include <cuda_runtime_api.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#endif

namespace scn::algorithm
{
namespace
{
#if SCN_ENABLE_TENSORRT
constexpr std::size_t minWidth = 1U << 13;
constexpr std::size_t maxWidth = 1U << 17;
constexpr std::array<const char*, 4> names{{"spectrum", "hm", "bw", "off"}};

class Logger final : public nvinfer1::ILogger
{
public:
    void log(Severity severity, const char* message) noexcept override
    {
        if (message && severity <= Severity::kWARNING) {
            try { text_ += message; text_ += '\n'; } catch (...) {}
        }
    }
    std::string text_;
};

void checkCuda(cudaError_t status, const char* operation)
{
    if (status != cudaSuccess)
        throw std::runtime_error(std::string(operation) + ": " + cudaGetErrorString(status));
}

std::vector<std::uint8_t> readBytes(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("Cannot open FFSCN engine: " + path.u8string());
    const auto end = file.tellg();
    if (end <= 0) throw std::runtime_error("FFSCN engine is empty.");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        throw std::runtime_error("Cannot read complete FFSCN engine.");
    return bytes;
}

class TensorRtFfscnBackend final : public IFfscnBackend
{
public:
    ~TensorRtFfscnBackend() override { release(); }

    bool initialize(const FfscnConfig& config, std::string& error) override
    {
        error.clear();
        release();
        try {
            if (config.deviceIndex < 0 || config.modelPath.empty())
                throw std::runtime_error("FFSCN needs an engine path and nonnegative GPU index.");
            device_ = config.deviceIndex;
            checkCuda(cudaSetDevice(device_), "cudaSetDevice");
            const auto trtVersion = getInferLibVersion();
            if (trtVersion != NV_TENSORRT_VERSION)
                throw std::runtime_error("TensorRT runtime/header version mismatch.");
            int cudaRuntime = 0, cudaDriver = 0;
            checkCuda(cudaRuntimeGetVersion(&cudaRuntime), "cudaRuntimeGetVersion");
            checkCuda(cudaDriverGetVersion(&cudaDriver), "cudaDriverGetVersion");
            cudaDeviceProp gpu{};
            checkCuda(cudaGetDeviceProperties(&gpu, device_), "cudaGetDeviceProperties");
            const auto path = std::filesystem::absolute(std::filesystem::u8path(config.modelPath)).lexically_normal();
            const auto bytes = readBytes(path);
            info_ = "backend=FFSCN17/TensorRT; engine=" + path.u8string() +
                "; bytes=" + std::to_string(bytes.size()) +
                "; sha256=" + detail::sha256(bytes.data(), bytes.size()) +
                "; TensorRT=" + std::to_string(trtVersion) +
                "; CUDA.headers=" + std::to_string(CUDART_VERSION) +
                "; CUDA.runtime=" + std::to_string(cudaRuntime) +
                "; CUDA.driver=" + std::to_string(cudaDriver) +
                "; GPU.index=" + std::to_string(device_) + "; GPU.name=" + gpu.name;
            runtime_.reset(nvinfer1::createInferRuntime(logger_));
            if (!runtime_) throw std::runtime_error("TensorRT createInferRuntime failed.");
            engine_.reset(runtime_->deserializeCudaEngine(bytes.data(), bytes.size()));
            if (!engine_) throw std::runtime_error("TensorRT could not deserialize FFSCN engine: " + logger_.text_);
            context_.reset(engine_->createExecutionContext());
            if (!context_) throw std::runtime_error("TensorRT createExecutionContext failed.");
            validateEngine();
            checkCuda(cudaStreamCreateWithFlags(&stream_, cudaStreamNonBlocking), "cudaStreamCreate");
            allocate(0, maxWidth * 10 * sizeof(float));
            for (std::size_t i = 1; i < buffers_.size(); ++i)
                allocate(i, (maxWidth / 4) * sizeof(float));
            info_ += "; profile=8192:131072; dtype=float32; outputs=hm,bw,off";
            return true;
        } catch (const std::exception& e) {
            error = e.what();
            info_ = "backend=FFSCN17/TensorRT; status=failed; error=" + error;
            release();
            return false;
        }
    }

    bool infer(const std::vector<float>& input, std::size_t width,
               FfscnModelOutput& output, std::string& error) override
    {
        error.clear();
        output = {};
        try {
            if (!context_ || width < minWidth || width > maxWidth || (width & (width - 1)) || input.size() != width * 10)
                throw std::runtime_error("Invalid FFSCN runtime input shape [1,1,10,N].");
            checkCuda(cudaSetDevice(device_), "cudaSetDevice");
            nvinfer1::Dims inputDims{};
            inputDims.nbDims = 4;
            inputDims.d[0] = 1; inputDims.d[1] = 1; inputDims.d[2] = 10;
            inputDims.d[3] = static_cast<std::int64_t>(width);
            if (!context_->setInputShape(names[0], inputDims))
                throw std::runtime_error("TensorRT rejected FFSCN dynamic input width " + std::to_string(width));
            const std::size_t inputBytes = input.size() * sizeof(float);
            std::memcpy(buffers_[0].host, input.data(), inputBytes);
            checkCuda(cudaMemcpyAsync(buffers_[0].device, buffers_[0].host, inputBytes,
                                      cudaMemcpyHostToDevice, stream_), "copy FFSCN input");
            for (std::size_t i = 0; i < names.size(); ++i) {
                if (!context_->setTensorAddress(names[i], buffers_[i].device))
                    throw std::runtime_error("TensorRT rejected tensor address " + std::string(names[i]));
            }
            const auto outDims = context_->getTensorShape(names[1]);
            const std::size_t outputLength = width / 4;
            if (outDims.nbDims != 4 || outDims.d[0] != 1 || outDims.d[1] != 1 ||
                outDims.d[2] != 1 || outDims.d[3] != static_cast<std::int64_t>(outputLength))
                throw std::runtime_error("FFSCN TensorRT output shape does not match [1,1,1,N/4].");
            for (std::size_t tensor = 2; tensor < names.size(); ++tensor) {
                const auto dims = context_->getTensorShape(names[tensor]);
                if (dims.nbDims != outDims.nbDims || dims.d[0] != outDims.d[0] ||
                    dims.d[1] != outDims.d[1] || dims.d[2] != outDims.d[2] || dims.d[3] != outDims.d[3])
                    throw std::runtime_error("FFSCN output tensors have inconsistent shapes.");
            }
            if (!context_->enqueueV3(stream_)) throw std::runtime_error("FFSCN TensorRT enqueueV3 failed.");
            const std::size_t outputBytes = outputLength * sizeof(float);
            for (std::size_t i = 1; i < buffers_.size(); ++i)
                checkCuda(cudaMemcpyAsync(buffers_[i].host, buffers_[i].device, outputBytes,
                                          cudaMemcpyDeviceToHost, stream_), "copy FFSCN output");
            checkCuda(cudaStreamSynchronize(stream_), "synchronize FFSCN inference");
            output.inputLength = width;
            std::array<std::vector<float>*, 3> destinations{{&output.heatmap, &output.bandwidth, &output.offset}};
            for (std::size_t i = 0; i < destinations.size(); ++i) {
                const auto* values = static_cast<const float*>(buffers_[i + 1].host);
                if (!std::all_of(values, values + outputLength, [](float value) { return std::isfinite(value); }))
                    throw std::runtime_error("FFSCN returned NaN/Inf in " + std::string(names[i + 1]));
                destinations[i]->assign(values, values + outputLength);
            }
            return true;
        } catch (const std::exception& e) {
            output = {};
            error = e.what();
            return false;
        }
    }

    std::string modelInfo() const override { return info_; }

private:
    struct Buffer { void* device = nullptr; void* host = nullptr; };
    void allocate(std::size_t index, std::size_t bytes)
    {
        checkCuda(cudaMalloc(&buffers_[index].device, bytes), "cudaMalloc FFSCN buffer");
        checkCuda(cudaHostAlloc(&buffers_[index].host, bytes, cudaHostAllocDefault), "cudaHostAlloc FFSCN buffer");
    }
    void validateEngine()
    {
        if (engine_->getNbIOTensors() != static_cast<int>(names.size()))
            throw std::runtime_error("FFSCN engine must expose spectrum, hm, bw and off.");
        for (int i = 0; i < engine_->getNbIOTensors(); ++i) {
            const char* name = engine_->getIOTensorName(i);
            if (!name || std::find_if(names.begin(), names.end(), [name](const char* expected) {
                    return std::strcmp(name, expected) == 0; }) == names.end())
                throw std::runtime_error("Unexpected FFSCN TensorRT I/O tensor.");
            const bool input = std::strcmp(name, names[0]) == 0;
            if (engine_->getTensorIOMode(name) != (input ? nvinfer1::TensorIOMode::kINPUT : nvinfer1::TensorIOMode::kOUTPUT) ||
                engine_->getTensorDataType(name) != nvinfer1::DataType::kFLOAT ||
                engine_->getTensorFormat(name, 0) != nvinfer1::TensorFormat::kLINEAR ||
                engine_->getTensorLocation(name) != nvinfer1::TensorLocation::kDEVICE ||
                engine_->isShapeInferenceIO(name))
                throw std::runtime_error("FFSCN TensorRT I/O contract must be linear float32.");
        }
        const auto input = engine_->getTensorShape(names[0]);
        if (input.nbDims != 4 || input.d[0] != 1 || input.d[1] != 1 || input.d[2] != 10 || input.d[3] != -1)
            throw std::runtime_error("FFSCN engine must have dynamic spectrum input [1,1,10,N].");
        const auto minimum = engine_->getProfileShape(names[0], 0, nvinfer1::OptProfileSelector::kMIN);
        const auto maximum = engine_->getProfileShape(names[0], 0, nvinfer1::OptProfileSelector::kMAX);
        if (minimum.nbDims != 4 || maximum.nbDims != 4 || minimum.d[3] > static_cast<std::int64_t>(minWidth) ||
            maximum.d[3] < static_cast<std::int64_t>(maxWidth))
            throw std::runtime_error("FFSCN optimization profile must cover widths 8192 through 131072.");
    }
    void release() noexcept
    {
        if (stream_) { cudaStreamSynchronize(stream_); }
        context_.reset(); engine_.reset(); runtime_.reset();
        for (auto& buffer : buffers_) {
            if (buffer.device) cudaFree(buffer.device);
            if (buffer.host) cudaFreeHost(buffer.host);
            buffer = {};
        }
        if (stream_) { cudaStreamDestroy(stream_); stream_ = nullptr; }
    }
    Logger logger_;
    std::unique_ptr<nvinfer1::IRuntime> runtime_;
    std::unique_ptr<nvinfer1::ICudaEngine> engine_;
    std::unique_ptr<nvinfer1::IExecutionContext> context_;
    std::array<Buffer, 4> buffers_{};
    cudaStream_t stream_ = nullptr;
    int device_ = 0;
    std::string info_ = "backend=FFSCN17/TensorRT; status=uninitialized";
};
#else
class TensorRtFfscnBackend final : public IFfscnBackend
{
public:
    bool initialize(const FfscnConfig&, std::string& error) override
    { error = "TensorRT FFSCN backend unavailable: built with SCN_ENABLE_TENSORRT=OFF"; return false; }
    bool infer(const std::vector<float>&, std::size_t, FfscnModelOutput& output, std::string& error) override
    { output = {}; error = "TensorRT FFSCN backend unavailable: built with SCN_ENABLE_TENSORRT=OFF"; return false; }
    std::string modelInfo() const override { return "backend=FFSCN17/TensorRT; status=unavailable"; }
};
#endif
} // namespace

std::unique_ptr<IFfscnBackend> createTensorRtFfscnBackend()
{ return std::make_unique<TensorRtFfscnBackend>(); }
} // namespace scn::algorithm
