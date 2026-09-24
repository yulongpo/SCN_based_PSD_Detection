#pragma once

#include "DetectionLabWorker.h"
#include "../../app/HaiAISpecMonitor/ui/DetectionConfigWidget.h"
#include "../../app/HaiAISpecMonitor/ui/FrequencySpinBox.h"
#include "../../app/HaiAISpecMonitor/ui/SpectrumWidget.h"

#include <QMainWindow>
#include <QThread>

#include <functional>
#include <memory>

class QLabel;
class QFormLayout;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QSplitter;
class QTabWidget;
class QTableWidget;
class QDoubleSpinBox;

namespace scn::lab
{

class DetectionLabWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit DetectionLabWindow(QWidget* parent = nullptr);
    ~DetectionLabWindow() override;

private slots:
    void browseInput();
    void browseConfig();
    void importConfig();
    void exportConfig();
    void inspectModel();
    void startRun();
    void pauseRun();
    void stepOnce();
    void seekToFrame();
    void stopSession();
    void refreshInputMetadata();
    void onSessionOpened(const LabSessionInfo& info);
    void onModelInspectionFinished(bool success, const QString& modelInfo, const QString& error);
    void onFrameReady(const LabFrameUpdate& update);
    void onPositionChanged(quint64 frameIndex);
    void onStateChanged(const QString& state);
    void onError(const QString& error);
    void onFinished();

private:
    void buildUi();
    QWidget* buildInputPage();
    QWidget* buildExportPage();
    QLineEdit* makePathRow(QFormLayout* form, const QString& label,
                           const QString& buttonText, const QString& objectName,
                           const std::function<void()>& browse);
    void beginSession();
    bool collectOptions(LabRunOptions& options, QString& error) const;
    void applyFrame(const LabFrameUpdate& update);
    void updateDetectionTables(const algorithm::DetectionResult& result);
    void updateControls();
    void updateMetadataHint();
    void appendLog(const QString& message);
    void showError(const QString& message);
    void zoomToRow(QTableWidget* table);
    QString formatFrequency(double hz) const;

    QThread m_workerThread;
    DetectionLabWorker* m_worker = nullptr;
    std::shared_ptr<std::atomic_bool> m_cancelRequested;

    QTabWidget* m_leftTabs = nullptr;
    QLineEdit* m_inputFile = nullptr;
    QLineEdit* m_configFile = nullptr;
    QLineEdit* m_outputFile = nullptr;
    QLineEdit* m_csvFile = nullptr;
    QLineEdit* m_dumpDirectory = nullptr;
    QLineEdit* m_referenceFile = nullptr;
    QLineEdit* m_startFrame = nullptr;
    QLineEdit* m_maximumFrames = nullptr;
    QLineEdit* m_seekFrame = nullptr;
    QSpinBox* m_logicalFps = nullptr;
    QSpinBox* m_pointCount = nullptr;
    app::FrequencySpinBox* m_centerFrequency = nullptr;
    app::FrequencySpinBox* m_span = nullptr;
    app::FrequencySpinBox* m_rbw = nullptr;
    QDoubleSpinBox* m_referenceLevel = nullptr;
    app::DetectionConfigWidget* m_detectionConfig = nullptr;
    QLabel* m_inputMetadata = nullptr;
    QLabel* m_modelStatus = nullptr;
    QLabel* m_stateLabel = nullptr;
    QLabel* m_frameLabel = nullptr;
    QLabel* m_statisticsLabel = nullptr;
    QProgressBar* m_progress = nullptr;
    QPushButton* m_inspectButton = nullptr;
    QPushButton* m_startButton = nullptr;
    QPushButton* m_pauseButton = nullptr;
    QPushButton* m_stepButton = nullptr;
    QPushButton* m_stopButton = nullptr;
    QPushButton* m_seekButton = nullptr;
    app::SpectrumWidget* m_spectrum = nullptr;
    QTabWidget* m_resultTabs = nullptr;
    QTableWidget* m_rawTable = nullptr;
    QTableWidget* m_trackedTable = nullptr;
    QTableWidget* m_channelTable = nullptr;
    QTableWidget* m_groupingTable = nullptr;
    QPlainTextEdit* m_log = nullptr;

    std::size_t m_frameCount = 0;
    std::size_t m_totalFrames = 0;
    std::size_t m_processedFrames = 0;
    bool m_sessionOpened = false;
    bool m_openPending = false;
    bool m_runAfterOpen = false;
    bool m_stepAfterOpen = false;
    bool m_isRunning = false;
    bool m_stopPending = false;
    bool m_hasCenterMetadata = false;
    bool m_hasSpanMetadata = false;
    bool m_hasRbwMetadata = false;
    bool m_hasReferenceMetadata = false;
    bool m_hasPointMetadata = false;
};

} // namespace scn::lab
