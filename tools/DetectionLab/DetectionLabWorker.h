#pragma once

#include "LabSession.h"

#include <QObject>

#include <atomic>
#include <memory>

namespace scn::lab
{

class DetectionLabWorker final : public QObject
{
    Q_OBJECT

public:
    explicit DetectionLabWorker(std::shared_ptr<std::atomic_bool> cancelRequested);

public slots:
    void openSession(LabRunOptions options);
    void inspectModel(algorithm::DetectionConfig config);
    void startRun();
    void pauseRun();
    void stepOnce();
    void seekTo(quint64 frameIndex);
    void stopSession();

signals:
    void sessionOpened(const scn::lab::LabSessionInfo& info);
    void modelInspectionFinished(bool success, const QString& modelInfo, const QString& error);
    void frameReady(const scn::lab::LabFrameUpdate& update);
    void positionChanged(quint64 frameIndex);
    void stateChanged(const QString& state);
    void errorOccurred(const QString& error);
    void finished();

private:
    void scheduleNext();
    void processNext();

    std::shared_ptr<std::atomic_bool> m_cancelRequested;
    std::unique_ptr<LabSession> m_session;
    bool m_running = false;
    bool m_stepRequested = false;
    bool m_scheduled = false;
};

} // namespace scn::lab
