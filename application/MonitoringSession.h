#pragma once

#include "../algorithm/types/DisplayTypes.h"
#include "../source/SourceConfig.h"
#include "../algorithm/DetectionConfig.h"

#include <QThread>
#include <QObject>
#include <QString>

Q_DECLARE_METATYPE(scn::algorithm::DisplaySnapshot)
Q_DECLARE_METATYPE(scn::algorithm::DisplaySnapshotPtr)

namespace scn::application
{

class SessionWorker;
class SessionPipeline;

class MonitoringSession final : public QObject
{
    Q_OBJECT

public:
    explicit MonitoringSession(QObject* parent = nullptr);
    ~MonitoringSession() override;

    void configure(const source::SourceConfig& config);
    void start();
    void pause();
    void resume();
    void stop();
    algorithm::ConfigApplyResult configureDetection(const algorithm::DetectionConfig& config);

signals:
    void snapshotReady(const algorithm::DisplaySnapshotPtr& snapshot);
    void stateChanged(const QString& state);
    void errorOccurred(const QString& message);
    void detectionStatusChanged(const QString& message);
    void deviceStatusChanged(const QString& device,
                             const QString& status,
                             bool connected);

private:
    QThread m_workerThread;
    SessionWorker* m_worker = nullptr;
    std::unique_ptr<SessionPipeline> m_pipeline;
    std::uint64_t m_publishedRevision = 0;
    void publishLatest();
};

} // namespace scn::application
