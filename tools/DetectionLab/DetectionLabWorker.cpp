#include "DetectionLabWorker.h"

#include <QTimer>

namespace scn::lab
{

DetectionLabWorker::DetectionLabWorker(std::shared_ptr<std::atomic_bool> cancelRequested)
    : m_cancelRequested(std::move(cancelRequested))
{
}

void DetectionLabWorker::openSession(LabRunOptions options)
{
    m_running = false;
    m_stepRequested = false;
    m_cancelRequested->store(false, std::memory_order_relaxed);
    m_session = std::make_unique<LabSession>();
    QString error;
    if (!m_session->open(options, error)) {
        m_session.reset();
        emit errorOccurred(error);
        emit stateChanged(QStringLiteral("打开失败"));
        return;
    }
    LabSessionInfo info;
    info.frameCount = m_session->frameCount();
    info.plannedFrames = m_session->totalFrames();
    info.startFrame = options.startFrame;
    info.centerFrequencyHz = m_session->centerFrequencyHz();
    info.spanHz = m_session->spanHz();
    info.resolutionBandwidthHz = m_session->resolutionBandwidthHz();
    info.referenceLevelDbm = m_session->referenceLevelDbm();
    info.metadata = m_session->metadata();
    info.modelInfo = m_session->modelInfo();
    info.stageDirectory = m_session->stageDirectory();
    emit sessionOpened(info);
    emit stateChanged(QStringLiteral("已就绪"));
}

void DetectionLabWorker::inspectModel(algorithm::DetectionConfig config)
{
    m_cancelRequested->store(false, std::memory_order_relaxed);
    QString modelInfo, error;
    const bool success = LabSession::inspectModel(config, modelInfo, error);
    emit modelInspectionFinished(success, modelInfo, error);
}

void DetectionLabWorker::startRun()
{
    if (!m_session) {
        emit errorOccurred(QStringLiteral("请先选择频谱文件并打开检测会话。"));
        return;
    }
    m_cancelRequested->store(false, std::memory_order_relaxed);
    m_running = true;
    m_stepRequested = false;
    emit stateChanged(QStringLiteral("正在处理"));
    scheduleNext();
}

void DetectionLabWorker::pauseRun()
{
    m_running = false;
    emit stateChanged(QStringLiteral("已暂停"));
}

void DetectionLabWorker::stepOnce()
{
    if (!m_session) {
        emit errorOccurred(QStringLiteral("请先打开检测会话。"));
        return;
    }
    m_running = false;
    m_stepRequested = true;
    m_cancelRequested->store(false, std::memory_order_relaxed);
    emit stateChanged(QStringLiteral("单帧处理"));
    scheduleNext();
}

void DetectionLabWorker::seekTo(quint64 frameIndex)
{
    m_running = false;
    m_stepRequested = false;
    if (!m_session) {
        emit errorOccurred(QStringLiteral("请先打开检测会话。"));
        return;
    }
    QString error;
    if (!m_session->seek(static_cast<std::size_t>(frameIndex), error)) {
        emit errorOccurred(error);
        return;
    }
    emit positionChanged(frameIndex);
    emit stateChanged(QStringLiteral("已跳转；时间历史已重置"));
}

void DetectionLabWorker::stopSession()
{
    m_running = false;
    m_stepRequested = false;
    if (m_session) {
        m_session->stop();
        m_session.reset();
    }
    m_cancelRequested->store(false, std::memory_order_relaxed);
    emit stateChanged(QStringLiteral("已停止"));
}

void DetectionLabWorker::scheduleNext()
{
    if (m_scheduled) return;
    if (!m_running && !m_stepRequested) return;
    m_scheduled = true;
    QTimer::singleShot(0, this, &DetectionLabWorker::processNext);
}

void DetectionLabWorker::processNext()
{
    m_scheduled = false;
    if (!m_session || (!m_running && !m_stepRequested)) return;
    LabFrameUpdate update;
    QString error;
    const auto status = m_session->processNext(update, error, [cancel = m_cancelRequested] {
        return cancel->load(std::memory_order_relaxed);
    });
    if (status == LabStepStatus::Error) {
        m_running = false;
        m_stepRequested = false;
        emit errorOccurred(error);
        emit stateChanged(QStringLiteral("处理失败"));
        return;
    }
    if (status == LabStepStatus::End) {
        m_running = false;
        m_stepRequested = false;
        emit finished();
        emit stateChanged(QStringLiteral("处理完成"));
        return;
    }
    if (status == LabStepStatus::Cancelled) {
        m_running = false;
        m_stepRequested = false;
        emit stateChanged(QStringLiteral("已停止"));
        return;
    }

    emit frameReady(update);
    emit positionChanged(static_cast<quint64>(m_session->position()));
    if (m_stepRequested) {
        m_stepRequested = false;
        emit stateChanged(QStringLiteral("单帧完成；已暂停"));
    } else if (m_running) {
        QTimer::singleShot(0, this, [this] { scheduleNext(); });
    }
}

} // namespace scn::lab
