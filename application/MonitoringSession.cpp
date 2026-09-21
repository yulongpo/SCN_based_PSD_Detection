#include "MonitoringSession.h"

#include "../algorithm/DetectionEngine/DetectionEngine.h"
#include "SourceManager.h"

#include <QMetaObject>
#include <QTimer>

#include <algorithm>
#include <string>
#include <utility>

namespace scn::application
{

class SessionWorker final : public QObject
{
    Q_OBJECT

public:
    explicit SessionWorker(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

public slots:
    void configure(source::SourceConfig config)
    {
        ensureTimer();
        m_timer->stop();
        m_running = false;
        m_paused = false;

        publishDeviceStatuses(algorithm::SourceKind::File, false);

        std::string error;
        if (!m_sources.configure(config, error)) {
            publishDeviceStatuses(config.kind, false);
            emit errorOccurred(QString::fromStdString(error));
            emit stateChanged(QStringLiteral("Source configuration failed"));
            return;
        }

        m_engine.reset();
        if (!m_engine.initialize(algorithm::DetectionConfig{})) {
            emit errorOccurred(QStringLiteral("DetectionEngine initialization failed."));
            return;
        }

        publishDeviceStatuses(config.kind, true);

        const int intervalMs = 1000 / std::max(1, config.frameRateHz);
        m_timer->setInterval(intervalMs);
        const QString sourceLabel = sourceName(config.kind) +
            (m_sources.config().filePath.empty()
                ? QString()
                : QStringLiteral(" / ") + QString::fromStdString(m_sources.config().filePath));
        emit stateChanged(QStringLiteral("Configured: %1").arg(sourceLabel));
    }

    void start()
    {
        ensureTimer();
        if (!m_sources.start()) {
            emit errorOccurred(QStringLiteral("The selected source is not configured."));
            return;
        }
        m_running = true;
        m_paused = false;
        m_timer->start();
        emit stateChanged(QStringLiteral("Running"));
    }

    void pause()
    {
        if (!m_running) return;
        m_paused = true;
        m_sources.pause(true);
        if (m_timer) m_timer->stop();
        emit stateChanged(QStringLiteral("Paused"));
    }

    void resume()
    {
        if (!m_running) return;
        m_paused = false;
        m_sources.pause(false);
        if (m_timer) m_timer->start();
        emit stateChanged(QStringLiteral("Running"));
    }

    void stop()
    {
        if (m_timer) m_timer->stop();
        m_sources.stop();
        m_running = false;
        m_paused = false;
        emit stateChanged(QStringLiteral("Stopped"));
    }

signals:
    void snapshotReady(const algorithm::DisplaySnapshotPtr& snapshot);
    void stateChanged(const QString& state);
    void errorOccurred(const QString& message);
    void deviceStatusChanged(const QString& device,
                             const QString& status,
                             bool connected);

private slots:
    void onTick()
    {
        if (!m_running || m_paused) return;

        algorithm::SpectrumFrame frame;
        if (!m_sources.read(frame)) {
            return;
        }

        auto snapshot = std::make_shared<algorithm::DisplaySnapshot>();
        snapshot->frame = std::move(frame);
        snapshot->detection = m_engine.process(snapshot->frame);
        snapshot->running = true;
        emit snapshotReady(snapshot);
    }

private:
    void publishDeviceStatuses(algorithm::SourceKind activeKind, bool configured)
    {
        const bool bb60cConnected = configured && activeKind == algorithm::SourceKind::BB60C;
        const bool harogicConnected = false;
        emit deviceStatusChanged(QStringLiteral("BB60C"),
                                 bb60cConnected ? QStringLiteral("已连接") : QStringLiteral("未连接"),
                                 bb60cConnected);
        emit deviceStatusChanged(QStringLiteral("海得罗捷"),
                                 harogicConnected ? QStringLiteral("已连接") : QStringLiteral("未接入"),
                                 harogicConnected);
    }

    void ensureTimer()
    {
        if (m_timer) return;
        m_timer = new QTimer(this);
        connect(m_timer, &QTimer::timeout, this, &SessionWorker::onTick);
    }

    static QString sourceName(algorithm::SourceKind kind)
    {
        switch (kind) {
        case algorithm::SourceKind::BB60C: return QStringLiteral("BB60C");
        case algorithm::SourceKind::Harogic: return QStringLiteral("Harogic");
        case algorithm::SourceKind::File: return QStringLiteral("File");
        }
        return QStringLiteral("Unknown");
    }

    SourceManager m_sources;
    algorithm::DetectionEngine m_engine;
    QTimer* m_timer = nullptr;
    bool m_running = false;
    bool m_paused = false;
};

MonitoringSession::MonitoringSession(QObject* parent)
    : QObject(parent), m_worker(new SessionWorker)
{
    qRegisterMetaType<algorithm::DisplaySnapshotPtr>("scn::algorithm::DisplaySnapshotPtr");

    m_worker->moveToThread(&m_workerThread);
    connect(&m_workerThread, &QThread::finished,
            m_worker, &QObject::deleteLater);
    connect(m_worker, &SessionWorker::snapshotReady,
            this, &MonitoringSession::snapshotReady,
            Qt::QueuedConnection);
    connect(m_worker, &SessionWorker::stateChanged,
            this, &MonitoringSession::stateChanged,
            Qt::QueuedConnection);
    connect(m_worker, &SessionWorker::errorOccurred,
            this, &MonitoringSession::errorOccurred,
            Qt::QueuedConnection);
    connect(m_worker, &SessionWorker::deviceStatusChanged,
            this, &MonitoringSession::deviceStatusChanged,
            Qt::QueuedConnection);
    m_workerThread.start();
}

MonitoringSession::~MonitoringSession()
{
    stop();
    m_workerThread.quit();
    m_workerThread.wait();
    m_worker = nullptr;
}

void MonitoringSession::configure(const source::SourceConfig& config)
{
    QMetaObject::invokeMethod(m_worker,
        [worker = m_worker, config] { worker->configure(config); },
        Qt::QueuedConnection);
}

void MonitoringSession::start()
{
    QMetaObject::invokeMethod(m_worker,
        [worker = m_worker] { worker->start(); },
        Qt::QueuedConnection);
}

void MonitoringSession::pause()
{
    QMetaObject::invokeMethod(m_worker,
        [worker = m_worker] { worker->pause(); },
        Qt::QueuedConnection);
}

void MonitoringSession::resume()
{
    QMetaObject::invokeMethod(m_worker,
        [worker = m_worker] { worker->resume(); },
        Qt::QueuedConnection);
}

void MonitoringSession::stop()
{
    if (!m_workerThread.isRunning()) return;
    QMetaObject::invokeMethod(m_worker,
        [worker = m_worker] { worker->stop(); },
        Qt::BlockingQueuedConnection);
}

} // namespace scn::application

#include "MonitoringSession.moc"
