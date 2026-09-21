#pragma once

#include "radioai/icd/HQSigMF.hpp"
#include "haisignal/HaiCudaBase.h"

/**
 * @file HQSigMFGPU.hpp
 * @brief HQSigMF 的 CPU/GPU Section 适配类型。
 *
 * 普通 Section 使用 malloc/free；本文件的 GPU 类型使用 haiCudaMalloc/
 * haiCudaFree，池化类型则把 shared_ptr<void> 作为数据所有权令牌保存到基类。
 * size 参数始终是字节数，调用方必须在构造前完成元素数到字节数的换算。
 */

/// 分配 GPU 内存，可选地把主机数据复制到设备。
struct SectionGPUByCPU : public HQSigMF::Section {
    SectionGPUByCPU(const HQSigMF::SectionProperty& prop, int32_t size, const void* in_data) : HQSigMF::Section(prop, 0, nullptr)
    {
        //重写数据分配规则
        if (0 < size) {
            haiCudaMalloc(&data.data, size);
            data.size = size;
            if (nullptr == data.data) throw std::runtime_error("Construction failed");
            if (nullptr != in_data)haiCudaMemcpy(data.data, in_data, size, HaiCudaMemcpyKind::HostToDevice);
        }
    }

    virtual ~SectionGPUByCPU() {
        if (nullptr != data.data) {
            haiCudaFree(data.data);
            data.data = nullptr;
        }
    }
};

/// 分配新的 GPU Section，并可选地从另一块设备内存复制数据。
struct SectionGPUByGPU : public HQSigMF::Section {
    SectionGPUByGPU(const HQSigMF::SectionProperty& prop, int32_t size, const void* in_data) : HQSigMF::Section(prop, 0, nullptr)
    {
        //重写数据分配规则
        if (0 < size) {
            haiCudaMalloc(&data.data, size);
            data.size = size;
            if (nullptr == data.data) throw std::runtime_error("Construction failed");
            if(nullptr != in_data)haiCudaMemcpy(data.data, in_data, size, HaiCudaMemcpyKind::DeviceToDevice);
        }
    }

    virtual ~SectionGPUByGPU() {
        if (nullptr != data.data) {
            haiCudaFree(data.data);
            data.data = nullptr;
        }
    }
};

/// 分配主机内存，并可选地把设备数据复制回主机。
struct SectionCPUByGPU : public HQSigMF::Section {
    SectionCPUByGPU(const HQSigMF::SectionProperty& prop, int32_t size, const void* in_data) : HQSigMF::Section(prop, 0, nullptr)
    {
        //重写数据分配规则
        if (0 < size) {
            data.data = malloc(size);
            data.size = size;
            if (nullptr == data.data) throw std::runtime_error("Construction failed");
            if (nullptr != in_data)haiCudaMemcpy(data.data, in_data, size, HaiCudaMemcpyKind::DeviceToHost);
        }
    }

    virtual ~SectionCPUByGPU() {
        if (nullptr != data.data) {
            free(data.data);
            data.data = nullptr;
        }
    }
};

/// 使用外部 CPU 池块承载数据，构造时执行 GPU→CPU 复制；Section 销毁时归还池块。
struct SectionCPUByGPUPool : public HQSigMF::Section
{
    SectionCPUByGPUPool(const HQSigMF::SectionProperty& prop,
                        std::shared_ptr<void> pool_buf, int32_t size,
                        const void* gpu_data)
        : HQSigMF::Section(prop, std::move(pool_buf), size)
    {
        if (nullptr != gpu_data && nullptr != data.data) {
            haiCudaMemcpy(data.data, gpu_data, size, HaiCudaMemcpyKind::DeviceToHost);
        }
    }
};

/// 使用外部 CPU 池块承载数据，构造时执行 CPU→CPU 复制。
struct SectionCPUByCPUPool : public HQSigMF::Section
{
    SectionCPUByCPUPool(const HQSigMF::SectionProperty& prop,
        std::shared_ptr<void> pool_buf, int32_t size,
        const void* src_data)
        : HQSigMF::Section(prop, std::move(pool_buf), size)
    {
        if (nullptr != src_data && nullptr != data.data) {
            memcpy(data.data, src_data, size);
        }
    }
};

/// 使用外部 GPU 池块承载数据，构造时执行 GPU→GPU 复制。
struct SectionGPUByGPUPool : public HQSigMF::Section
{
    SectionGPUByGPUPool(const HQSigMF::SectionProperty& prop,
        std::shared_ptr<void> pool_buf, int32_t size,
        const void* gpu_data)
        : HQSigMF::Section(prop, std::move(pool_buf), size)
    {
        if (nullptr != gpu_data && nullptr != data.data) {
            haiCudaMemcpy(data.data, gpu_data, size, HaiCudaMemcpyKind::DeviceToDevice);
        }
    }
};
