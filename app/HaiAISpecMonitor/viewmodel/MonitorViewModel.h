#pragma once

#include "../../../algorithm/types/DisplayTypes.h"

#include <QObject>

namespace scn::app::viewmodel
{

class MonitorViewModel final : public QObject
{
    Q_OBJECT

public:
    explicit MonitorViewModel(QObject* parent = nullptr);

public slots:
    void acceptSnapshot(const algorithm::DisplaySnapshotPtr& snapshot);
    void acceptState(const QString& state);
    void clear();

signals:
    void snapshotChanged(const algorithm::DisplaySnapshotPtr& snapshot);
    void stateChanged(const QString& state);

private:
    algorithm::DisplaySnapshotPtr m_snapshot;
};

} // namespace scn::app::viewmodel
