#pragma once

#include <cstddef>
#include <memory>
#include <vector>

namespace scn::runtime
{

class BufferPool
{
public:
    BufferPool() = default;
    explicit BufferPool(std::size_t bufferSize, std::size_t count)
        : m_bufferSize(bufferSize), m_buffers(count, std::vector<float>(bufferSize))
    {
    }

    void reset(std::size_t bufferSize, std::size_t count)
    {
        m_bufferSize = bufferSize;
        m_buffers.assign(count, std::vector<float>(bufferSize));
    }

    [[nodiscard]] std::size_t bufferSize() const noexcept { return m_bufferSize; }
    [[nodiscard]] std::size_t size() const noexcept { return m_buffers.size(); }

private:
    std::size_t m_bufferSize = 0;
    std::vector<std::vector<float>> m_buffers;
};

} // namespace scn::runtime
