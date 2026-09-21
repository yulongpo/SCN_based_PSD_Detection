#include "MonitoringSession.h"
#include "SessionPipeline.h"
#include "SourceManager.h"

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
    void configure(const source::SourceConfig& config, std::uint64_t epoch, std::uint64_t control)
    {
        if (epoch != m_pipeline.generation) return;
        ensureTimer(); m_timer->stop(); m_running = false; m_paused = false; m_epoch = epoch; m_control = control;
        m_lastTimestamp = -1;
        publishDevices(false);
        std::string error;
        if (!m_sources.configure(config, error)) {
            m_pipeline.setActive(epoch, false);
            emit errorOccurred(m_control, QString::fromStdString(error));
            emit stateChanged(m_control, QStringLiteral("Source configuration failed"));
            return;
        }
        publishDevices(true);
        m_timer->setInterval(std::max(1, 1000 / std::max(1, config.frameRateHz)));
        emit stateChanged(m_control, QStringLiteral("Configured"));
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
        m_sources.stop(); m_running = false; m_paused = false; m_epoch = epoch; m_control = control;
        m_pipeline.setActive(epoch, false);
        emit stateChanged(m_control, QStringLiteral("Stopped"));
    }
signals:
    void stateChanged(std::uint64_t epoch, const QString& state);
    void errorOccurred(std::uint64_t epoch, const QString& message);
    void deviceStatusChanged(std::uint64_t epoch, const QString& device,
                             const QString& status, bool connected);
private:
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
        m_pipeline.submit(std::move(frame), m_epoch, !file);
    }
    void ensureTimer()
    {
        if (m_timer) return;
        m_timer = new QTimer(this);
        m_timer->setTimerType(Qt::PreciseTimer);
        connect(m_timer, &QTimer::timeout, this, &SessionWorker::onTick);
    }
    void publishDevices(bool configured)
    {
        const bool bb = configured && m_sources.kind() == algorithm::SourceKind::BB60C;
        emit deviceStatusChanged(m_control, QStringLiteral("BB60C"),
            bb ? QStringLiteral("已连接") : QStringLiteral("未连接"), bb);
        emit deviceStatusChanged(m_control, QStringLiteral("海得罗捷"), QStringLiteral("未接入"), false);
    }
    SessionPipeline& m_pipeline;
    SourceManager m_sources;
    QTimer* m_timer = nullptr;
    std::uint64_t m_epoch = 0, m_control = 0;
    std::int64_t m_lastTimestamp = -1;
    bool m_running = false, m_paused = false;
};

MonitoringSession::MonitoringSession(QObject* parent) : QObject(parent)
{
    qRegisterMetaType<algorithm::DisplaySnapshotPtr>("scn::algorithm::DisplaySnapshotPtr");
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
    m_workerThread.start();
    auto* timer = new QTimer(this);
    timer->setInterval(33);
    connect(timer, &QTimer::timeout, this, &MonitoringSession::publishLatest);
    timer->start();
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

void MonitoringSession::configure(const source::SourceConfig& config)
{
    const auto epoch = m_pipeline->newEpoch();
    // Existing clients that do not supply detection settings still get the default model.
    if (!m_pipeline->configVersion) configureDetection(algorithm::DetectionConfig{});
    const auto control = m_pipeline->controlGeneration.load();
    QMetaObject::invokeMethod(m_worker, [worker = m_worker, config, epoch, control] {
        worker->configure(config, epoch, control);
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

void MonitoringSession::start()
{
    const auto epoch = m_pipeline->generation.load();
    m_pipeline->setActive(epoch, true); // Prevent a model restart while source start is queued.
    QMetaObject::invokeMethod(m_worker, [worker = m_worker, epoch] { worker->start(epoch); }, Qt::QueuedConnection);
}
void MonitoringSession::pause()
{
    const auto epoch = m_pipeline->controlGeneration.load();
    QMetaObject::invokeMethod(m_worker, [worker = m_worker, epoch] { worker->pause(epoch); }, Qt::QueuedConnection);
}
void MonitoringSession::resume()
{
    const auto epoch = m_pipeline->controlGeneration.load();
    QMetaObject::invokeMethod(m_worker, [worker = m_worker, epoch] { worker->resume(epoch); }, Qt::QueuedConnection);
}
void MonitoringSession::stop()
{
    const auto epoch = m_pipeline->newEpoch(); // Immediately invalidate queued and in-flight output.
    const auto control = m_pipeline->controlGeneration.load();
    QMetaObject::invokeMethod(m_worker, [worker = m_worker, epoch, control] { worker->stop(epoch, control); }, Qt::QueuedConnection);
}
void MonitoringSession::publishLatest()
{
    std::shared_ptr<const algorithm::SpectrumFrame> frame;
    std::shared_ptr<const algorithm::DetectionResult> result;
    std::uint64_t epoch, version;
    {
        std::lock_guard<std::mutex> lock(m_pipeline->mutex);
        if (m_publishedRevision == m_pipeline->revision) return;
        m_publishedRevision = m_pipeline->revision;
        frame = m_pipeline->latestFrame; result = m_pipeline->latestResult;
        epoch = m_pipeline->generation; version = m_pipeline->configVersion;
    }
    if (!frame) return; // Preserve stopped image; starting/source change clears it in MainWindow.
    auto snapshot = std::make_shared<algorithm::DisplaySnapshot>();
    snapshot->frame = *frame; // At most one raw-frame copy per 33 ms publication.
    snapshot->detection.generation = epoch;
    snapshot->detection.configVersion = version;
    snapshot->detection.requiredFrames = m_pipeline->detectionConfig().accumulator.frames;
    if (result && result->generation == epoch && result->configVersion == version &&
        result->startFrequencyHz == frame->startFrequencyHz &&
        result->binWidthHz == frame->binWidthHz && result->pointCount == frame->powerDb.size() &&
        result->referenceLevelDbm == frame->referenceLevelDbm &&
        result->resolutionBandwidthHz == frame->resolutionBandwidthHz && result->sourceName == frame->sourceName)
        snapshot->detection = *result;
    else snapshot->detection.diagnostics.message = "SCN waiting for a compatible detection result.";
    snapshot->running = m_pipeline->active;
    snapshot->droppedFrames = m_pipeline->queue.dropped();
    emit snapshotReady(snapshot);
}
} // namespace scn::application

#include "MonitoringSession.moc"
