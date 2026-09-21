#include "SourceManager.h"
#include "../source/FileSource/FileSource.h"

#include <utility>

namespace scn::application
{

bool SourceManager::configure(const source::SourceConfig& config, std::string& error)
{
    stop();
    m_source.reset();
    auto sourceAdapter = source::createSource(config.kind);
    if (!sourceAdapter || !sourceAdapter->open(config, error)) {
        return false;
    }
    m_config = config;
    m_source = std::move(sourceAdapter);
    return true;
}

bool SourceManager::start()
{
    return m_source && m_source->start();
}

void SourceManager::pause(bool paused)
{
    if (m_source) m_source->pause(paused);
}

void SourceManager::stop()
{
    if (m_source) m_source->stop();
}

bool SourceManager::read(algorithm::SpectrumFrame& frame)
{
    return m_source && m_source->read(frame);
}

bool SourceManager::atFileEnd() const
{
    const auto* file = dynamic_cast<const source::FileSource*>(m_source.get());
    return file && file->position() >= file->frameCount();
}

} // namespace scn::application
