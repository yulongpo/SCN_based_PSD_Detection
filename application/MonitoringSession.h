#pragma once

#include "../algorithm/types/DisplayTypes.h"
#include "../source/SourceConfig.h"
#include "../algorithm/DetectionConfig.h"
#include "RecordingConfig.h"
#include "policy/PolicyTypes.h"

#include <QThread>
#include <QObject>
#include <QString>

Q_DECLARE_METATYPE(scn::algorithm::DisplaySnapshot)
Q_DECLARE_METATYPE(scn::algorithm::DisplaySnapshotPtr)
Q_DECLARE_METATYPE(scn::application::policy::PolicySnapshotPtr)
Q_DECLARE_METATYPE(std::vector<scn::application::policy::AlarmEventChange>)

class QTimer;

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
    void configureRecording(const RecordingConfig& config);
    void start();
    void pause();
    void resume();
    void stop();
    void setPublicationRateHz(int rateHz);
    algorithm::ConfigApplyResult configureDetection(const algorithm::DetectionConfig& config);
    bool configurePolicy(const policy::PolicyConfig& config, QString& error);
    policy::PolicyConfig policyConfig() const;
    bool acknowledgeAlarm(const QString& eventId, const QString& note);
    bool loadAlarmHistory(std::vector<policy::AlarmEvent>& events, QString& error) const;

signals:
    void snapshotReady(const algorithm::DisplaySnapshotPtr& snapshot);
    void stateChanged(const QString& state);
    void errorOccurred(const QString& message);
    void detectionStatusChanged(const QString& message);
    void policySnapshotReady(const scn::application::policy::PolicySnapshotPtr& snapshot);
    void alarmEventsReady(const std::vector<scn::application::policy::AlarmEventChange>& changes);
    void policyStatusChanged(const QString& message);
    void deviceStatusChanged(const QString& device,
                             const QString& status,
                             bool connected);
    void recordingStatusChanged(const QString& path,
                                double startFrequencyHz,
                                double endFrequencyHz,
                                double resolutionBandwidthHz,
                                std::uint64_t frameCount,
                                bool active);
    void recordingError(const QString& message);

private:
    QThread m_workerThread;
    SessionWorker* m_worker = nullptr;
    std::unique_ptr<SessionPipeline> m_pipeline;
    std::uint64_t m_publishedRevision = 0;
    std::uint64_t m_publishedPolicyRevision = 0;
    QTimer* m_publishTimer = nullptr;
    int m_publicationRateHz = 30;
    void publishLatest();
};

} // namespace scn::application
