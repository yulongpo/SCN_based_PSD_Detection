#include "PresentationModel.h"

namespace scn::application
{

PresentationModel::PresentationModel(QObject* parent)
    : QObject(parent)
{
}

void PresentationModel::acceptSnapshot(const algorithm::DisplaySnapshotPtr& snapshot)
{
    m_snapshot = snapshot;
    m_hasSnapshot = true;
    emit snapshotChanged(m_snapshot);
}

void PresentationModel::clear()
{
    m_snapshot.reset();
    m_hasSnapshot = false;
}

} // namespace scn::application
