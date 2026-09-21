#include "SourceFactory.h"

#include "BB60CSource/BB60CSource.h"
#include "FileSource/FileSource.h"
#include "HarogicSource/HarogicSource.h"

namespace scn::source
{

std::unique_ptr<ISpectrumSource> createSource(algorithm::SourceKind kind)
{
    switch (kind) {
    case algorithm::SourceKind::BB60C:
        return std::make_unique<BB60CSource>();
    case algorithm::SourceKind::Harogic:
        return std::make_unique<HarogicSource>();
    case algorithm::SourceKind::File:
        return std::make_unique<FileSource>();
    }
    return nullptr;
}

} // namespace scn::source
