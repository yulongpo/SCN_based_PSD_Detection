#pragma once

#include "../algorithm/types/DisplayTypes.h"

#include <QObject>

namespace scn::application
{

class PresentationModel final : public QObject
{
    Q_OBJECT

public:
    explicit PresentationModel(QObject* parent = nullptr);

    [[nodiscard]] const algorithm::DisplaySnapshotPtr& snapshot() const noexcept
    {
        return m_snapshot;
    }

    [[nodiscard]] bool hasSnapshot() const noexcept { return m_hasSnapshot; }

public slots:
    void acceptSnapshot(const algorithm::DisplaySnapshotPtr& snapshot);
    void clear();

signals:
    void snapshotChanged(const algorithm::DisplaySnapshotPtr& snapshot);

private:
    algorithm::DisplaySnapshotPtr m_snapshot;
    bool m_hasSnapshot = false;
};

} // namespace scn::application
