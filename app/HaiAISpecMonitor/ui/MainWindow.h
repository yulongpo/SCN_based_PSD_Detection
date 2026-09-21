#pragma once

#include "PlaybackPage.h"
#include "SettingsPage.h"
#include "FrequencyNavigatorWidget.h"
#include "SpectrumWidget.h"
#include "StatusBarWidget.h"
#include "WaterfallWidget.h"
#include "../controller/MainController.h"
#include "../viewmodel/MonitorViewModel.h"

#include "../../../application/MonitoringSession.h"
#include "../../../application/PresentationModel.h"
#include "../../../application/policy/PolicyTypes.h"

#include <QMainWindow>
#include <QByteArray>
#include <QPoint>
#include <QRect>
#include <QElapsedTimer>

#include <optional>
#include <tuple>

class QCheckBox;
class QCloseEvent;
class QComboBox;
class QDoubleSpinBox;
class QEvent;
class QFrame;
class QLabel;
class QLineEdit;
class QMouseEvent;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QSplitter;
class QTableWidget;
class QTimer;

namespace scn::app
{

namespace policy = scn::application::policy;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(application::MonitoringSession& session,
               application::PresentationModel& presentationModel,
               QWidget* parent = nullptr);
    ~MainWindow() override = default;

private slots:
    void browseFile();
    void sourceSelectionChanged(int index);
    void applyConfiguration();
    void startMonitoring();
    void pauseMonitoring();
    void stopMonitoring();
    void selectMainPage(int index);
    void showAbout();
    void toggleMaximize();
    void onSnapshot(const algorithm::DisplaySnapshotPtr& snapshot);
    void refreshDisplay();
    void onStateChanged(const QString& state);
    void onError(const QString& message);
    void onReplayRequested(const QString& path);
    void updateRuntimeStatus();
    void applyPolicyConfiguration();
    void onPolicySnapshot(const scn::application::policy::PolicySnapshotPtr& snapshot);
    void onAlarmEvents(const std::vector<scn::application::policy::AlarmEventChange>& changes);
    void onPolicyStatus(const QString& message);
    void showAlarmHistory();

protected:
    void closeEvent(QCloseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    bool nativeEvent(const QByteArray& eventType, void* message,
                     qintptr* result) override;

private:
    void buildUi();
    void buildTitleBar();
    void buildMonitorPage();
    void buildMonitorControlPanel(QWidget* parent);
    void buildSignalTable(QWidget* parent);
    void buildMenus();
    void applyTheme();
    void updateSourceControls();
    void updateDisplayDomain();
    void syncFrequencyNavigator();
    void updateButtonState(const QString& state);
    void updateMonitorMetrics(const algorithm::DisplaySnapshot& snapshot);
    void applyFileMetadata(const QString& path);
    void loadUiState();
    void saveUiState() const;
    bool applyCurrentConfiguration();
    bool applyDetectionConfiguration();
    void clearDetectionDisplay();
    void clearMonitoringDisplay(bool discardCurrentGeneration = true);
    void updateDetectionStatus(const algorithm::DisplaySnapshot* snapshot);
    void updateSignalTable(const algorithm::DisplaySnapshot& snapshot);
    const policy::SignalAnnotation* annotationFor(policy::PolicySignalSource source,
                                                  std::int64_t signalId) const;
    QString formatDetectionTime(std::int64_t timestampNs, bool fileSource) const;
    QString signalDetails(const policy::PolicySignal& signal, bool fileSource) const;
    source::SourceConfig currentConfig() const;
    bool validateConfiguration(const source::SourceConfig& config, QString& error) const;
    QString formatFrequency(double hz, int decimals = 3) const;
    Qt::Edges resizeEdgesAt(const QPoint& globalPosition) const;
    void updateResizeCursor(const QPoint& globalPosition);
    void restoreResizeCursor();
    void applyManualResize(const QPoint& globalPosition);
    void finishManualResize();

    application::MonitoringSession& m_session;
    application::PresentationModel& m_presentationModel;
    controller::MainController m_controller;
    viewmodel::MonitorViewModel m_viewModel;

    QWidget* m_titleBar = nullptr;
    QPushButton* m_monitorTab = nullptr;
    QPushButton* m_playbackTab = nullptr;
    QPushButton* m_settingsTab = nullptr;
    QPushButton* m_minimizeButton = nullptr;
    QPushButton* m_maximizeButton = nullptr;
    QPushButton* m_closeButton = nullptr;
    QStackedWidget* m_pages = nullptr;
    QWidget* m_monitorPage = nullptr;

    QComboBox* m_sourceCombo = nullptr;
    QWidget* m_centerGroup = nullptr;
    QWidget* m_bandwidthGroup = nullptr;
    QWidget* m_fileSourceGroup = nullptr;
    QWidget* m_resolutionBandwidthGroup = nullptr;
    QWidget* m_rbwShapeGroup = nullptr;
    QLineEdit* m_filePath = nullptr;
    QPushButton* m_browseButton = nullptr;
    QDoubleSpinBox* m_centerFrequency = nullptr;
    QDoubleSpinBox* m_bandwidth = nullptr;
    QDoubleSpinBox* m_startFrequency = nullptr;
    QDoubleSpinBox* m_endFrequency = nullptr;
    QDoubleSpinBox* m_resolutionBandwidth = nullptr;
    QDoubleSpinBox* m_referenceLevel = nullptr;
    QComboBox* m_rbwShape = nullptr;
    QSpinBox* m_pointCount = nullptr;
    QSpinBox* m_frameRate = nullptr;
    QCheckBox* m_loopFile = nullptr;
    QPushButton* m_applyButton = nullptr;
    QPushButton* m_startButton = nullptr;
    QPushButton* m_pauseButton = nullptr;
    QLabel* m_stateLabel = nullptr;
    QLabel* m_frameLabel = nullptr;
    QLabel* m_detectionStatusLabel = nullptr;
    QLabel* m_criticalAlertLabel = nullptr;
    QLabel* m_generalAlarmLabel = nullptr;
    QLabel* m_signalTotalLabel = nullptr;
    StatusBarWidget* m_statusStrip = nullptr;
    QSplitter* m_plotSplitter = nullptr;
    SpectrumWidget* m_spectrum = nullptr;
    WaterfallWidget* m_waterfall = nullptr;
    FrequencyNavigatorWidget* m_frequencyNavigator = nullptr;
    QTableWidget* m_signalTable = nullptr;
    PlaybackPage* m_playbackPage = nullptr;
    SettingsPage* m_settingsPage = nullptr;
    QTimer* m_statusTimer = nullptr;
    QTimer* m_displayTimer = nullptr;
    algorithm::DisplaySnapshotPtr m_pendingSnapshot;
    algorithm::DisplaySnapshotPtr m_displaySnapshot;
    std::optional<algorithm::DetectionConfig> m_appliedDetectionConfig;
    policy::PolicySnapshotPtr m_policySnapshot;
    policy::PolicySnapshotPtr m_pendingPolicySnapshot;
    std::uint64_t m_policyEventRevision = 0;
    using DetectionKey = std::tuple<std::uint64_t, std::uint64_t, std::uint64_t,
                                    algorithm::DetectionStage>;
    std::optional<DetectionKey> m_lastDetectionKey;
    QString m_detectionModelStatus;
    std::uint64_t m_latestGeneration = 0;
    std::uint64_t m_minimumGeneration = 0;
    std::int64_t m_timeOriginNs = 0;
    QElapsedTimer m_displayRateTimer;
    std::uint64_t m_displayRateFrames = 0;
    double m_displayRateHz = 0;

    bool m_monitoring = false;
    bool m_dragging = false;
    bool m_manualResizing = false;
    bool m_resizeCursorOverridden = false;
    bool m_updatingFrequency = false;
    bool m_fileFrequencyMetadataLocked = false;
    bool m_fileRbwMetadataLocked = false;
    bool m_fileReferenceMetadataLocked = false;
    QPoint m_dragOffset;
    QPoint m_resizePressPosition;
    QRect m_resizeGeometry;
    Qt::Edges m_resizeEdges;
};

} // namespace scn::app
