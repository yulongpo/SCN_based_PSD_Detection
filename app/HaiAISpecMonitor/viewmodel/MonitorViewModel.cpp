#include "MonitorViewModel.h"

namespace scn::app::viewmodel
{

MonitorViewModel::MonitorViewModel(QObject* parent)
    : QObject(parent)
{
}

void MonitorViewModel::acceptSnapshot(const algorithm::DisplaySnapshotPtr& snapshot)
{
    m_snapshot = snapshot;
    emit snapshotChanged(m_snapshot);
}

void MonitorViewModel::acceptState(const QString& state)
{
    emit stateChanged(state);
}

void MonitorViewModel::clear()
{
    m_snapshot.reset();
}

} // namespace scn::app::viewmodel
