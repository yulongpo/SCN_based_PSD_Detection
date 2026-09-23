#include "MonitoringSession.h"
#include "SessionPipeline.h"
#include "SourceManager.h"
#include "../source/SpectrumRecorder.h"

#include <QCoreApplication>
#include <QDir>
#include <QMetaObject>
#include <QTimer>
#include <algorithm>

namespace scn::application
{
class SessionWorker final : public QObject
{
    Q_OBJECT
public:
    explicit SessionWorker(SessionPipeline& pipeline) : m_pipeline(pipeline) {}
    void configure(const source::SourceConfig& config, std::uint64_t epoch,
                   std::uint64_t control, bool resumeRequested, bool pausedRequested)
    {
        if (epoch != m_pipeline.generation) return;
        const bool resumeAfterConfigure = m_running || resumeRequested;
        const bool remainPaused = m_paused || (resumeRequested && pausedRequested);
        ensureTimer();
        m_timer->stop();
        m_running = false;
        m_paused = false;
        m_epoch = epoch;
        m_control = control;
        finishRecording();
        m_lastTimestamp = -1;
        std::string error;
        if (!m_sources.configure(config, error)) {
            publishDevices();
            m_pipeline.setActive(epoch, false);
            if (epoch != m_pipeline.generation) return;
            emit errorOccurred(m_control, QString::fromStdString(error));
            emit stateChanged(m_control, QStringLiteral("Source configuration failed"));
            return;
        }
        if (epoch != m_pipeline.generation) {
            m_sources.stop();
            return;
        }
        publishDevices();
        m_timer->setInterval(std::max(1, 1000 / std::max(1, config.frameRateHz)));
        if (!resumeAfterConfigure) {
            emit stateChanged(m_control, QStringLiteral("Configured"));
            return;
        }
        if (!m_sources.start()) {
            m_pipeline.setActive(epoch, false);
            emit errorOccurred(m_control, QStringLiteral("The selected source could not resume after reconfiguration."));
            emit stateChanged(m_control, QStringLiteral("Source restart failed"));
            return;
        }
        if (!m_pipeline.setActive(epoch, true)) {
            m_sources.stop();
            return;
        }
        m_running = true;
        m_paused = remainPaused;
        if (remainPaused) {
            m_sources.pause(true);
        } else {
            m_timer->start();
        }
        emit stateChanged(m_control, remainPaused ? QStringLiteral("Paused")
                                                  : QStringLiteral("Running"));
    }
    void setRecordingConfig(const RecordingConfig& config)
    {
        if (m_running) return;
        m_recordingConfig = config;
    }
    void startDeviceMonitoring()
    {
        m_control = m_pipeline.controlGeneration.load();
        m_epoch = m_pipeline.generation.load();
        if (!m_deviceTimer) {
            m_deviceTimer = new QTimer(this);
            m_deviceTimer->setInterval(2000);
            connect(m_deviceTimer, &QTimer::timeout, this, &SessionWorker::publishDevices);
        }
        publishDevices();
        if (!m_deviceTimer->isActive()) m_deviceTimer->start();
    }
    void start(std::uint64_t epoch)
    {
        if (epoch != m_pipeline.generation || epoch != m_epoch) return;
        ensureTimer();
        if (!m_sources.start()) {
            m_pipeline.setActive(epoch, false);
            emit errorOccurred(m_control, QStringLiteral("The selected source is not configured."));
            emit stateChanged(m_control, QStringLiteral("Source start failed"));
            return;
        }
        if (!m_pipeline.setActive(epoch, true)) { m_sources.stop(); return; }
        m_running = true; m_paused = false;
        m_timer->start();
        emit stateChanged(m_control, QStringLiteral("Running"));
    }
    void pause(std::uint64_t controlEpoch)
    {
        if (!m_running || controlEpoch != m_pipeline.controlGeneration || m_epoch != m_pipeline.generation) return;
        m_paused = true; m_sources.pause(true); m_timer->stop();
        emit stateChanged(m_control, QStringLiteral("Paused"));
    }
    void resume(std::uint64_t controlEpoch)
    {
        if (!m_running || controlEpoch != m_pipeline.controlGeneration || m_epoch != m_pipeline.generation) return;
        m_paused = false; m_sources.pause(false); m_timer->start();
        emit stateChanged(m_control, QStringLiteral("Running"));
    }
    void stop(std::uint64_t epoch, std::uint64_t control)
    {
        if (epoch != m_pipeline.generation) return;
        if (m_timer) m_timer->stop();
        m_sources.stop();
        m_running = false; m_paused = false; m_epoch = epoch; m_control = control;
        finishRecording();
        m_pipeline.setActive(epoch, false);
        emit stateChanged(m_control, QStringLiteral("Stopped"));
    }
signals:
    void stateChanged(std::uint64_t epoch, const QString& state);
    void errorOccurred(std::uint64_t epoch, const QString& message);
    void deviceStatusChanged(std::uint64_t epoch, const QString& device,
                             const QString& status, bool connected);
    void recordingStatusChanged(std::uint64_t epoch, const QString& path,
                                double startFrequencyHz, double endFrequencyHz,
                                double resolutionBandwidthHz, std::uint64_t frameCount,
                                bool active);
    void recordingError(std::uint64_t epoch, const QString& message);
private:
    void finishRecording()
    {
        if (!m_recorder.isRecording()) return;
        const auto path = QString::fromStdString(m_recorder.path());
        const auto startHz = m_recorder.startFrequencyHz();
        const auto endHz = m_recorder.endFrequencyHz();
        const auto rbwHz = m_recorder.resolutionBandwidthHz();
        const auto frames = m_recorder.frameCount();
        m_recorder.stop();
        emit recordingStatusChanged(m_control, path, startHz, endHz, rbwHz, frames, false);
    }

    void onTick()
    {
        if (!m_running || m_paused || m_epoch != m_pipeline.generation || m_pipeline.closing) return;
        const bool file = m_sources.kind() == algorithm::SourceKind::File;
        if (file && !m_pipeline.queue.hasCapacity()) return;
        // Do not discard the last queued/in-flight frames when a looping file wraps.
        if (file && m_sources.atFileEnd()) {
            if (!m_pipeline.drained(m_epoch)) return;
            if (m_sources.config().loopFile) {
                const auto epoch = m_pipeline.newEpoch(m_epoch);
                if (!epoch) return;
                m_epoch = epoch;
                m_lastTimestamp = -1;
            } else {
                m_running = false; m_pipeline.setActive(m_epoch, false); m_timer->stop();
                emit stateChanged(m_control, QStringLiteral("Stopped (file completed)"));
                return;
            }
        }
        auto frame = std::make_shared<algorithm::SpectrumFrame>();
        if (!m_sources.read(*frame)) {
            finishRecording();
            if (file) {
                m_running = false; m_pipeline.setActive(m_epoch, false); m_timer->stop();
                emit stateChanged(m_control, QStringLiteral("Stopped (file completed)"));
            }
            return;
        }
        if (m_epoch != m_pipeline.generation) return;
        if (file && m_lastTimestamp >= 0 && frame->timestampNs < m_lastTimestamp) {
            const auto epoch = m_pipeline.newEpoch(m_epoch);
            if (!epoch) return;
            m_epoch = epoch;
        }
        m_lastTimestamp = frame->timestampNs;
        if (!file && m_recordingConfig.enabled) {
            std::string recordError;
            if (!m_recorder.isRecording()) {
                if (m_recorder.start(*frame, m_recordingConfig, recordError)) {
                    emit recordingStatusChanged(m_control,
                        QString::fromStdString(m_recorder.path()),
                        m_recorder.startFrequencyHz(), m_recorder.endFrequencyHz(),
                        m_recorder.resolutionBandwidthHz(), 0, true);
                } else {
                    emit recordingError(m_control, QString::fromStdString(recordError));
                    m_recordingConfig.enabled = false;
                }
            }
            if (m_recorder.isRecording() && !m_recorder.write(*frame, recordError)) {
                emit recordingError(m_control, QString::fromStdString(recordError));
                finishRecording();
                m_recordingConfig.enabled = false;
            } else if (m_recorder.isRecording() &&
                       (m_recorder.frameCount() == 1 || m_recorder.frameCount() % 30 == 0)) {
                emit recordingStatusChanged(m_control,
                    QString::fromStdString(m_recorder.path()),
                    m_recorder.startFrequencyHz(), m_recorder.endFrequencyHz(),
                    m_recorder.resolutionBandwidthHz(), m_recorder.frameCount(), true);
            }
        }
        m_pipeline.submit(std::move(frame), m_epoch, !file);
    }
    void ensureTimer()
    {
        if (m_timer) return;
        m_timer = new QTimer(this);
        m_timer->setTimerType(Qt::PreciseTimer);
        connect(m_timer, &QTimer::timeout, this, &SessionWorker::onTick);
    }
    void publishDevices()
    {
        auto presence = source::probeLiveSourcePresence();
        presence.bb60c = presence.bb60c ||
            m_sources.hasOpenLiveSource(algorithm::SourceKind::BB60C);
        presence.harogic = presence.harogic ||
            m_sources.hasOpenLiveSource(algorithm::SourceKind::Harogic);
        emit deviceStatusChanged(m_control, QStringLiteral("BB60C"),
            presence.bb60c ? QStringLiteral("已连接") : QStringLiteral("未连接"), presence.bb60c);
        emit deviceStatusChanged(m_control, QStringLiteral("海得罗捷"),
            presence.harogic ? QStringLiteral("已连接") : QStringLiteral("未接入"), presence.harogic);
    }
    SessionPipeline& m_pipeline;
    SourceManager m_sources;
    QTimer* m_timer = nullptr;
    QTimer* m_deviceTimer = nullptr;
    std::uint64_t m_epoch = 0, m_control = 0;
    std::int64_t m_lastTimestamp = -1;
    RecordingConfig m_recordingConfig;
    source::SpectrumRecorder m_recorder;
    bool m_running = false, m_paused = false;
};

MonitoringSession::MonitoringSession(QObject* parent) : QObject(parent)
{
    qRegisterMetaType<algorithm::DisplaySnapshotPtr>("scn::algorithm::DisplaySnapshotPtr");
    qRegisterMetaType<policy::PolicySnapshotPtr>("scn::application::policy::PolicySnapshotPtr");
    qRegisterMetaType<std::vector<policy::AlarmEventChange>>("scn::application::policy::AlarmEvents");
    m_pipeline = std::make_unique<SessionPipeline>([this](std::uint64_t control, std::uint64_t version, const std::string& status) {
        const auto text = QString::fromStdString(status);
        QMetaObject::invokeMethod(this, [this, control, version, text] {
            if (control == m_pipeline->controlGeneration && version == m_pipeline->configVersion)
                emit detectionStatusChanged(text);
        }, Qt::QueuedConnection);
    });
    m_worker = new SessionWorker(*m_pipeline);
    m_worker->moveToThread(&m_workerThread);
    connect(&m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &SessionWorker::stateChanged, this,
        [this](std::uint64_t epoch, const QString& state) {
            if (epoch == m_pipeline->controlGeneration) emit stateChanged(state);
        }, Qt::QueuedConnection);
    connect(m_worker, &SessionWorker::errorOccurred, this,
        [this](std::uint64_t epoch, const QString& error) {
            if (epoch == m_pipeline->controlGeneration) emit errorOccurred(error);
        }, Qt::QueuedConnection);
    connect(m_worker, &SessionWorker::deviceStatusChanged, this,
        [this](std::uint64_t epoch, const QString& device, const QString& status, bool connected) {
            if (epoch == m_pipeline->controlGeneration) emit deviceStatusChanged(device, status, connected);
        }, Qt::QueuedConnection);
    connect(m_worker, &SessionWorker::recordingStatusChanged, this,
        [this](std::uint64_t epoch, const QString& path, double startHz, double endHz,
               double rbwHz, std::uint64_t frames, bool active) {
            if (epoch == m_pipeline->controlGeneration)
                emit recordingStatusChanged(path, startHz, endHz, rbwHz, frames, active);
        }, Qt::QueuedConnection);
    connect(m_worker, &SessionWorker::recordingError, this,
        [this](std::uint64_t epoch, const QString& message) {
            if (epoch == m_pipeline->controlGeneration) emit recordingError(message);
        }, Qt::QueuedConnection);
    m_workerThread.start();
    m_publishTimer = new QTimer(this);
    m_publishTimer->setInterval(1000 / m_publicationRateHz);
    connect(m_publishTimer, &QTimer::timeout, this, &MonitoringSession::publishLatest);
    m_publishTimer->start();
}

MonitoringSession::~MonitoringSession()
{
    m_pipeline->closing = true;
    m_pipeline->queue.close();
    m_workerThread.quit();
    m_workerThread.wait(); // Only destruction joins; ordinary stop never blocks the UI.
    m_pipeline->shutdown();
    m_worker = nullptr;
}

void MonitoringSession::setPublicationRateHz(int rateHz)
{
    m_publicationRateHz = std::clamp(rateHz, 1, 120);
    if (m_publishTimer) m_publishTimer->setInterval(1000 / m_publicationRateHz);
}

void MonitoringSession::configure(const source::SourceConfig& config)
{
    const bool resumeRequested = m_pipeline->active.load();
    const bool pausedRequested = m_paused.load();
    const auto epoch = m_pipeline->newEpoch();
    // Existing clients that do not supply detection settings still get the default model.
    if (!m_pipeline->configVersion) configureDetection(algorithm::DetectionConfig{});
    const auto control = m_pipeline->controlGeneration.load();
    QMetaObject::invokeMethod(m_worker,
        [worker = m_worker, config, epoch, control, resumeRequested, pausedRequested] {
        worker->configure(config, epoch, control, resumeRequested, pausedRequested);
    }, Qt::QueuedConnection);
}

void MonitoringSession::configureRecording(const RecordingConfig& config)
{
    QMetaObject::invokeMethod(m_worker, [worker = m_worker, config] {
        worker->setRecordingConfig(config);
    }, Qt::QueuedConnection);
}

void MonitoringSession::startDeviceMonitoring()
{
    QMetaObject::invokeMethod(m_worker, [worker = m_worker] {
        worker->startDeviceMonitoring();
    }, Qt::QueuedConnection);
}

algorithm::ConfigApplyResult MonitoringSession::configureDetection(const algorithm::DetectionConfig& requested)
{
    auto config = requested;
    if (QDir::isRelativePath(QString::fromStdString(config.detector.modelPath)))
        config.detector.modelPath = QDir(QCoreApplication::applicationDirPath())
            .absoluteFilePath(QString::fromStdString(config.detector.modelPath)).toStdString();
    const auto result = m_pipeline->configureDetection(config);
    if (result == algorithm::ConfigApplyResult::RequiresRestart && m_pipeline->active)
        emit detectionStatusChanged(QStringLiteral("请先停止监测，再更换模型、GPU 或切换检测开关。"));
    return result;
}

bool MonitoringSession::configurePolicy(const policy::PolicyConfig& config, QString& error)
{
    std::string reason;
    const bool ok = m_pipeline->configurePolicy(config, reason);
    error = QString::fromStdString(reason);
    if (ok) emit policyStatusChanged(QStringLiteral("白名单与告警规则已提交，版本 %1").arg(config.version));
    return ok;
}

policy::PolicyConfig MonitoringSession::policyConfig() const
{
    return m_pipeline->policyConfig();
}

bool MonitoringSession::acknowledgeAlarm(const QString& eventId, const QString& note)
{
    return m_pipeline->acknowledgeAlarm(eventId.toStdString(), note.toStdString());
}

bool MonitoringSession::loadAlarmHistory(std::vector<policy::AlarmEvent>& events, QString& error) const
{
    std::string reason;
    const bool ok = m_pipeline->loadAlarmHistory(events, reason);
    error = QString::fromStdString(reason);
    return ok;
}

void MonitoringSession::start()
{
    m_paused = false;
    const auto epoch = m_pipeline->generation.load();
    m_pipeline->setActive(epoch, true); // Prevent a model restart while source start is queued.
    QMetaObject::invokeMethod(m_worker, [worker = m_worker, epoch] { worker->start(epoch); }, Qt::QueuedConnection);
}
void MonitoringSession::pause()
{
    m_paused = true;
    const auto epoch = m_pipeline->controlGeneration.load();
    QMetaObject::invokeMethod(m_worker, [worker = m_worker, epoch] { worker->pause(epoch); }, Qt::QueuedConnection);
}
void MonitoringSession::resume()
{
    m_paused = false;
    const auto epoch = m_pipeline->controlGeneration.load();
    QMetaObject::invokeMethod(m_worker, [worker = m_worker, epoch] { worker->resume(epoch); }, Qt::QueuedConnection);
}
void MonitoringSession::stop()
{
    m_paused = false;
    const auto epoch = m_pipeline->newEpoch(); // Immediately invalidate queued and in-flight output.
    const auto control = m_pipeline->controlGeneration.load();
    QMetaObject::invokeMethod(m_worker, [worker = m_worker, epoch, control] { worker->stop(epoch, control); }, Qt::QueuedConnection);
}
void MonitoringSession::publishLatest()
{
    std::shared_ptr<const algorithm::SpectrumFrame> frame;
    std::shared_ptr<const algorithm::DetectionResult> result;
    std::uint64_t epoch, version, policyRevision;
    policy::PolicySnapshotPtr policySnapshot;
    std::vector<policy::AlarmEventChange> alarmChanges;
    {
        std::lock_guard<std::mutex> lock(m_pipeline->mutex);
        if (m_publishedRevision == m_pipeline->revision) return;
        m_publishedRevision = m_pipeline->revision;
        frame = m_pipeline->latestFrame; result = m_pipeline->latestResult;
        epoch = m_pipeline->generation; version = m_pipeline->configVersion;
        policyRevision = m_pipeline->policyRevision;
        policySnapshot = m_pipeline->latestPolicy;
        alarmChanges.swap(m_pipeline->pendingAlarmChanges);
    }
    if (frame) {
        auto snapshot = std::make_shared<algorithm::DisplaySnapshot>();
        snapshot->frame = *frame; // At most one raw-frame copy per publication tick.
        snapshot->detection.generation = epoch;
        snapshot->detection.configVersion = version;
        snapshot->detection.requiredFrames = m_pipeline->detectionConfig().accumulator.frames;
        if (result && result->generation == epoch && result->configVersion == version &&
            result->startFrequencyHz == frame->startFrequencyHz &&
            result->binWidthHz == frame->binWidthHz && result->pointCount == frame->powerDb.size() &&
            result->referenceLevelDbm == frame->referenceLevelDbm &&
            result->resolutionBandwidthHz == frame->resolutionBandwidthHz && result->sourceName == frame->sourceName) {
            // The display only consumes result metadata and timing diagnostics;
            // the raw candidate/evidence arrays can be tens of thousands of
            // cells and remain available on the pipeline result for DetectionLab.
            auto& display = snapshot->detection;
            display.sequence = result->sequence;
            display.generation = result->generation;
            display.configVersion = result->configVersion;
            display.trackingSegment = result->trackingSegment;
            display.firstSequence = result->firstSequence;
            display.firstTimestampNs = result->firstTimestampNs;
            display.timestampNs = result->timestampNs;
            display.accumulatedFrames = result->accumulatedFrames;
            display.requiredFrames = result->requiredFrames;
            display.startFrequencyHz = result->startFrequencyHz;
            display.binWidthHz = result->binWidthHz;
            display.pointCount = result->pointCount;
            display.referenceLevelDbm = result->referenceLevelDbm;
            display.resolutionBandwidthHz = result->resolutionBandwidthHz;
            display.sourceName = result->sourceName;
            display.stage = result->stage;
            display.trackingApplied = result->trackingApplied;
            display.channelAggregationApplied = result->channelAggregationApplied;
            display.diagnostics = result->diagnostics;
        } else {
            snapshot->detection.diagnostics.message = "SCN waiting for a compatible detection result.";
        }
        snapshot->running = m_pipeline->active;
        snapshot->droppedFrames = m_pipeline->queue.dropped();
        emit snapshotReady(snapshot);
    }
    if (policySnapshot && policyRevision != m_publishedPolicyRevision) {
        m_publishedPolicyRevision = policyRevision;
        emit policySnapshotReady(policySnapshot);
    }
    if (!alarmChanges.empty()) emit alarmEventsReady(alarmChanges);
}
} // namespace scn::application

#include "MonitoringSession.moc"
