#pragma once

#include "PlaybackPage.h"
#include "SettingsPage.h"
#include "SpectrumWidget.h"
#include "StatusBarWidget.h"
#include "WaterfallWidget.h"
#include "../controller/MainController.h"
#include "../viewmodel/MonitorViewModel.h"

#include "../../../application/MonitoringSession.h"
#include "../../../application/PresentationModel.h"

#include <QMainWindow>
#include <QPoint>

class QCheckBox;
class QCloseEvent;
class QComboBox;
class QDoubleSpinBox;
class QFrame;
class QLabel;
class QLineEdit;
class QMouseEvent;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTableWidget;
class QTimer;

namespace scn::app
{

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

protected:
    void closeEvent(QCloseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void buildUi();
    void buildTitleBar();
    void buildMonitorPage();
    void buildMonitorControlPanel(QWidget* parent);
    void buildSignalTable(QWidget* parent);
    void buildMenus();
    void applyTheme();
    void updateSourceControls();
    void updateButtonState(const QString& state);
    void updateMonitorMetrics(const algorithm::DisplaySnapshot& snapshot);
    void applyFileMetadata(const QString& path);
    source::SourceConfig currentConfig() const;
    QString formatFrequency(double hz, int decimals = 3) const;

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
    QLineEdit* m_filePath = nullptr;
    QPushButton* m_browseButton = nullptr;
    QDoubleSpinBox* m_startFrequency = nullptr;
    QDoubleSpinBox* m_endFrequency = nullptr;
    QDoubleSpinBox* m_resolutionBandwidth = nullptr;
    QDoubleSpinBox* m_referenceLevel = nullptr;
    QSpinBox* m_pointCount = nullptr;
    QSpinBox* m_frameRate = nullptr;
    QCheckBox* m_loopFile = nullptr;
    QPushButton* m_applyButton = nullptr;
    QPushButton* m_startButton = nullptr;
    QPushButton* m_pauseButton = nullptr;
    QLabel* m_stateLabel = nullptr;
    QLabel* m_frameLabel = nullptr;
    QLabel* m_criticalAlertLabel = nullptr;
    QLabel* m_generalAlarmLabel = nullptr;
    QLabel* m_signalTotalLabel = nullptr;
    StatusBarWidget* m_statusStrip = nullptr;
    SpectrumWidget* m_spectrum = nullptr;
    WaterfallWidget* m_waterfall = nullptr;
    QTableWidget* m_signalTable = nullptr;
    PlaybackPage* m_playbackPage = nullptr;
    SettingsPage* m_settingsPage = nullptr;
    QTimer* m_statusTimer = nullptr;
    QTimer* m_displayTimer = nullptr;
    algorithm::DisplaySnapshotPtr m_pendingSnapshot;

    bool m_monitoring = false;
    bool m_dragging = false;
    QPoint m_dragOffset;
};

} // namespace scn::app
