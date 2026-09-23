#include "MainWindow.h"
#include "FrequencySpinBox.h"
#include "common/Frequency.h"
#include "../../../source/FileSource/FileSource.h"

#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QColor>
#include <QCursor>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QSplitter>
#include <QSettings>
#include <QStackedWidget>
#include <QStyle>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>
#include <QValidator>
#include <QWindow>

#include <algorithm>
#include <cmath>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace scn::app
{

namespace
{
QPushButton* headerButton(const QString& text, QWidget* parent)
{
    auto* button = new QPushButton(text, parent);
    button->setObjectName(QStringLiteral("headerButton"));
    button->setCheckable(true);
    button->setFixedHeight(50);
    button->setIconSize(QSize(16, 16));
    return button;
}

QPushButton* windowButton(const QString& text, QWidget* parent)
{
    auto* button = new QPushButton(text, parent);
    button->setObjectName(QStringLiteral("windowButton"));
    button->setFixedSize(50, 50);
    return button;
}

QLabel* label(const QString& text, QWidget* parent, const QString& objectName = {})
{
    auto* result = new QLabel(text, parent);
    if (!objectName.isEmpty()) result->setObjectName(objectName);
    return result;
}

QWidget* metricCard(const QString& iconPath, const QString& title, const QString& color,
                    QLabel*& value, QWidget* parent)
{
    auto* card = new QFrame(parent);
    card->setObjectName(QStringLiteral("metricCard"));
    card->setMinimumSize(136, 76);
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    card->setToolTip(title);

    auto* layout = new QHBoxLayout(card);
    layout->setContentsMargins(10, 8, 12, 8);
    layout->setSpacing(10);

    const QColor accent(color);
    const QString iconBackground = QStringLiteral("rgba(%1, %2, %3, 38)")
                                       .arg(accent.red())
                                       .arg(accent.green())
                                       .arg(accent.blue());
    auto* iconLabel = new QLabel(card);
    iconLabel->setObjectName(QStringLiteral("metricIcon"));
    iconLabel->setPixmap(QPixmap(iconPath).scaled(24, 24, Qt::KeepAspectRatio,
                                                    Qt::SmoothTransformation));
    iconLabel->setStyleSheet(QStringLiteral(
        "background:%1; border:1px solid %2; border-radius:8px;").arg(iconBackground, color));
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setFixedSize(40, 40);

    auto* textLayout = new QVBoxLayout;
    textLayout->setContentsMargins(0, 1, 0, 1);
    textLayout->setSpacing(1);
    auto* titleLabel = new QLabel(title, card);
    titleLabel->setObjectName(QStringLiteral("metricTitle"));
    titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    value = new QLabel(QStringLiteral("0"), card);
    value->setObjectName(QStringLiteral("metricValue"));
    value->setStyleSheet(QStringLiteral("color:%1;").arg(color));
    value->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    value->setMinimumWidth(48);
    textLayout->addWidget(titleLabel);
    textLayout->addWidget(value);
    layout->addWidget(iconLabel);
    layout->addLayout(textLayout, 1);
    return card;
}

QTableWidgetItem* tableItem(const QString& text)
{
    auto* result = new QTableWidgetItem(text);
    result->setTextAlignment(Qt::AlignCenter);
    return result;
}

QString defaultSpectrumFilePath()
{
    return QStringLiteral(
        R"(D:\project\isa\bin\data\spectrum_data_org\20260911_152444_651_Fc=2025000000_Bw=3950000000_Rbw=50000_Reflevel=-20.0_SpectrumLen=202242.dat)");
}

bool hasDetectionObservations(const algorithm::DetectionResult& result)
{
    return result.stage == algorithm::DetectionStage::Accumulating ||
           result.stage == algorithm::DetectionStage::Completed;
}

bool sameSourceConfig(const source::SourceConfig& lhs, const source::SourceConfig& rhs)
{
    return lhs.kind == rhs.kind && lhs.filePath == rhs.filePath &&
        lhs.centerFrequencyHz == rhs.centerFrequencyHz &&
        lhs.bandwidthHz == rhs.bandwidthHz &&
        lhs.resolutionBandwidthHz == rhs.resolutionBandwidthHz &&
        lhs.referenceLevelDbm == rhs.referenceLevelDbm &&
        lhs.rbwShape == rhs.rbwShape && lhs.pointCount == rhs.pointCount &&
        lhs.frameRateHz == rhs.frameRateHz && lhs.loopFile == rhs.loopFile;
}
}

MainWindow::MainWindow(application::MonitoringSession& session,
                       application::PresentationModel& presentationModel,
                       QWidget* parent)
    : QMainWindow(parent),
      m_session(session),
      m_presentationModel(presentationModel),
      m_controller(session),
      m_viewModel(this)
{
    setWindowTitle(QStringLiteral("智能频谱监测仪"));
    setWindowIcon(QIcon(QStringLiteral(":/title/app.ico")));
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
    setMinimumSize(1280, 760);
    resize(1920, 1080);
    setMouseTracking(true);
    applyTheme();
    buildUi();
    buildMenus();
    qApp->installEventFilter(this);
    // The frameless window is covered by child widgets.  Enable hover events
    // on the existing widget tree so the global edge hit test can update the
    // resize cursor even when the pointer is over a chart or control.
    for (QWidget* child : findChildren<QWidget*>()) {
        child->setMouseTracking(true);
    }

    connect(&m_session, &application::MonitoringSession::snapshotReady,
            &m_presentationModel, &application::PresentationModel::acceptSnapshot);
    connect(&m_presentationModel, &application::PresentationModel::snapshotChanged,
            &m_viewModel, &viewmodel::MonitorViewModel::acceptSnapshot);
    connect(&m_session, &application::MonitoringSession::stateChanged,
            &m_viewModel, &viewmodel::MonitorViewModel::acceptState);
    connect(&m_session, &application::MonitoringSession::errorOccurred,
            this, &MainWindow::onError);
    connect(&m_session, &application::MonitoringSession::detectionStatusChanged,
            this, [this](const QString& message) {
                m_detectionModelStatus = message;
                if (message.contains(QStringLiteral("unavailable"), Qt::CaseInsensitive)) {
                    clearDetectionDisplay();
                }
                updateDetectionStatus(m_displaySnapshot.get());
            });
    connect(&m_session, &application::MonitoringSession::deviceStatusChanged,
            this, [this](const QString& device, const QString& status, bool connected) {
                m_statusStrip->setDeviceConnectionStatus(device, status, connected);
                bool* known = nullptr;
                bool* current = nullptr;
                if (device.compare(QStringLiteral("BB60C"), Qt::CaseInsensitive) == 0) {
                    known = &m_bb60cPresenceKnown;
                    current = &m_bb60cConnected;
                } else if (device.contains(QStringLiteral("海得罗捷")) ||
                           device.compare(QStringLiteral("Harogic"), Qt::CaseInsensitive) == 0) {
                    known = &m_harogicPresenceKnown;
                    current = &m_harogicConnected;
                }
                if (!known || !current) return;
                const bool wasKnown = *known;
                const bool wasConnected = *current;
                *known = true;
                *current = connected;

                const auto selectedKind = static_cast<algorithm::SourceKind>(
                    m_sourceCombo->currentData().toInt());
                const bool isSelectedDevice =
                    (selectedKind == algorithm::SourceKind::BB60C && known == &m_bb60cPresenceKnown) ||
                    (selectedKind == algorithm::SourceKind::Harogic && known == &m_harogicPresenceKnown);
                if (isSelectedDevice && connected) m_missingLiveSourceWarningShown = false;

                if (m_waitingForInitialDeviceProbe && isSelectedDevice) {
                    m_waitingForInitialDeviceProbe = false;
                    if (connected) (void)applyCurrentConfiguration();
                    else warnForMissingSelectedLiveSource();
                } else if (isSelectedDevice && !connected && (!wasKnown || wasConnected)) {
                    warnForMissingSelectedLiveSource();
                }
                updateStartButtonAvailability();
            });
    m_session.startDeviceMonitoring();
    connect(&m_session, &application::MonitoringSession::recordingStatusChanged,
            this, &MainWindow::onRecordingStatus);
    connect(&m_session, &application::MonitoringSession::recordingError,
            this, &MainWindow::onRecordingError);
    connect(&m_viewModel, &viewmodel::MonitorViewModel::snapshotChanged,
            this, &MainWindow::onSnapshot);
    connect(&m_viewModel, &viewmodel::MonitorViewModel::stateChanged,
            this, &MainWindow::onStateChanged);
    connect(m_playbackPage, &PlaybackPage::replayRequested,
            this, &MainWindow::onReplayRequested);
    connect(m_playbackPage, &PlaybackPage::logMessage,
            m_settingsPage, [this](const QString& message) {
                statusBar()->showMessage(message, 4000);
            });
    connect(m_settingsPage, &SettingsPage::logMessage,
            this, [this](const QString& message) {
                statusBar()->showMessage(message, 4000);
            });
    connect(m_settingsPage, &SettingsPage::displayRefreshRateChanged,
            this, [this](int rateHz) {
                const int safeRateHz = std::clamp(rateHz, 1, 120);
                if (m_displayTimer) m_displayTimer->setInterval(1000 / safeRateHz);
                m_session.setPublicationRateHz(safeRateHz);
            });
    connect(m_settingsPage, &SettingsPage::displayDynamicRangeChanged,
            this, [this](double rangeDb) {
                m_spectrum->setDynamicRangeDb(rangeDb);
                m_waterfall->setDynamicRangeDb(rangeDb);
                statusBar()->showMessage(QStringLiteral("显示动态范围已应用。"), 3000);
            });
    connect(m_settingsPage, &SettingsPage::detectionApplyRequested,
            this, [this] { (void)applyDetectionConfiguration(); });
    connect(m_settingsPage, &SettingsPage::policyApplyRequested,
            this, &MainWindow::applyPolicyConfiguration);
    connect(m_settingsPage, &SettingsPage::alarmHistoryRequested,
            this, &MainWindow::showAlarmHistory);
    connect(&m_session, &application::MonitoringSession::policySnapshotReady,
            this, &MainWindow::onPolicySnapshot);
    connect(&m_session, &application::MonitoringSession::alarmEventsReady,
            this, &MainWindow::onAlarmEvents);
    connect(&m_session, &application::MonitoringSession::policyStatusChanged,
            this, &MainWindow::onPolicyStatus);
    // ISA 中频谱图是频率视图的交互主控，瀑布图跟随同一范围和选中频点重算。
    connect(m_spectrum, &SpectrumWidget::viewRangeChanged,
            this, [this](double startHz, double endHz) {
                m_waterfall->setFrequencyView(startHz, endHz);
                m_frequencyNavigator->setViewRange(startHz, endHz);
            });
    connect(m_frequencyNavigator, &FrequencyNavigatorWidget::viewRangeRequested,
            this, [this](double startHz, double endHz) {
                m_spectrum->setFrequencyView(startHz, endHz);
            });
    connect(m_spectrum, &SpectrumWidget::frequencySelected,
            m_waterfall, &WaterfallWidget::setSelectedFrequency);

    m_statusTimer = new QTimer(this);
    m_statusTimer->setInterval(1000);
    connect(m_statusTimer, &QTimer::timeout, this, &MainWindow::updateRuntimeStatus);
    m_statusTimer->start();

    m_displayTimer = new QTimer(this);
    const int displayRateHz = std::clamp(m_settingsPage->displayRefreshRateHz(), 1, 120);
    m_displayTimer->setInterval(1000 / displayRateHz);
    connect(m_displayTimer, &QTimer::timeout, this, &MainWindow::refreshDisplay);
    m_displayTimer->start();
    m_session.setPublicationRateHz(displayRateHz);

    loadUiState();
    updateSourceControls();
    updateDisplayDomain();
    (void)applyDetectionConfiguration();
    applyPolicyConfiguration();
    const auto initialSourceKind = static_cast<algorithm::SourceKind>(m_sourceCombo->currentData().toInt());
    if (initialSourceKind == algorithm::SourceKind::File) {
        applyConfiguration();
    } else {
        // Wait for the asynchronous hardware probe before opening a live
        // source so an absent device does not produce a startup-open error.
        m_waitingForInitialDeviceProbe = true;
        updateStartButtonAvailability();
    }
    updateRuntimeStatus();
}

void MainWindow::applyTheme()
{
    setStyleSheet(QStringLiteral(R"(
        QMainWindow, QWidget { background: #060610; color: #d6dee8; font-family: "Microsoft YaHei"; }
        QFrame#titleBar { background: #131416; border: 0; }
        QLabel#appTitle { color: #ffffff; font-size: 26px; font-weight: 700; }
        QPushButton#headerButton { background: transparent; border: 0; color: rgba(255,255,255,200); font-size: 14px; font-weight: 600; padding: 0 14px; }
        QPushButton#headerButton:hover { color: #0a8cfe; }
        QPushButton#headerButton:checked { color: #0a8cfe; background-image: url(:/title/btn_active.png); background-position: bottom center; background-repeat: no-repeat; }
        QPushButton#windowButton { background: transparent; border: 0; color: #f3f6fb; }
        QPushButton#windowButton:hover { background: rgba(255,255,255,24); }
        QPushButton#closeWindowButton:hover { background: rgb(232,17,35); }
        QFrame#controlPanel, QFrame#displayPanel, QFrame#signalPanel, QFrame#statusStrip,
        QFrame#settingsCard { background: #060610; border: 1px solid rgba(255,255,255,40); border-radius: 12px; }
        QLabel#controlLabel { color: rgba(255,255,255,200); font-size: 13px; }
        QLabel#stateLabel { color: #0a8cfe; font-size: 13px; }
        QLabel#frameLabel, QLabel#pageHint { color: #606d79; font-size: 12px; }
        QLabel#settingsSectionTitle { color: #dcecff; font-size: 15px; font-weight: 600; padding-top: 8px; }
        QLabel#pageTitle { color: #e9f2fc; font-size: 22px; font-weight: 700; }
        QLabel#detailSummary { background: #15151e; border: 1px solid #1e1e28; border-radius: 8px; color: #c8c8c8; padding: 12px; }
        QLineEdit, QComboBox, QDoubleSpinBox, QSpinBox { background: #15151e; border: 1px solid #1e1e28; border-radius: 6px; color: #d6dee8; padding: 3px 8px; min-height: 26px; }
        QLineEdit:focus, QComboBox:focus, QDoubleSpinBox:focus, QSpinBox:focus { border: 1px solid #0a8cfe; }
        QComboBox QAbstractItemView { background: #15151e; color: #d6dee8; selection-background-color: #2c3e76; }
        QPushButton#primaryButton, QPushButton#dangerButton, QPushButton#pageToolButton { border-radius: 6px; min-height: 32px; padding: 0 14px; font-size: 13px; }
        QPushButton#primaryButton { background: #0a8cfe; border: 1px solid #0a8cfe; color: #d6ecff; }
        QPushButton#primaryButton:hover { background: #2c9eff; }
        QPushButton#dangerButton { background: rgba(230,62,62,48); border: 1px solid #8d2e38; color: #e63e3e; }
        QPushButton#dangerButton:hover { background: rgba(230,62,62,72); }
        QPushButton#pageToolButton { background: #15151e; border: 1px solid #1e1e28; color: #c8c8c8; }
        QPushButton#pageToolButton:hover { background: #202d58; border-color: #0a8cfe; }
        QCheckBox { color: #b7c8da; spacing: 8px; }
        QCheckBox::indicator { width: 16px; height: 16px; }
        QCheckBox::indicator:unchecked { background: #101a28; border: 1px solid #46617c; border-radius: 3px; }
        QCheckBox::indicator:checked { background: #0e8de2; border: 1px solid #33b9ff; border-radius: 3px; }
        QFrame#metricCard { background: #0b0f18; border: 1px solid #253348; border-radius: 10px; }
        QFrame#metricCard:hover { background: #101827; border-color: #3c628d; }
        QFrame#controlDivider { background: #263242; border: none; }
        QLabel#metricIcon { border-radius: 8px; }
        QLabel#metricTitle { color: #a0afbf; font-size: 12px; font-weight: 600; }
        QLabel#metricValue { font-size: 26px; font-weight: 700; }
        QFrame#spectrumTraceToolbar { background: rgba(7,31,61,230); border: 1px solid #1a304b; border-radius: 6px; }
        QPushButton#spectrumTraceButton { background: #071f3d; color: #d6ecff; border: 1px solid #1a304b; border-radius: 5px; padding: 0 8px; font-size: 11px; }
        QPushButton#spectrumTraceButton:hover { border-color: #3d78ad; }
        QPushButton#spectrumTraceButton:checked { background: #0a8cfe; border-color: #38b4ff; color: #ffffff; }
        QTableWidget#isaTable { background: #060610; alternate-background-color: #15151e; border: 1px solid #1e1e28; border-radius: 8px; gridline-color: #1e1e28; color: #c8c8c8; selection-background-color: #2c3e76; }
        QTableWidget#isaTable QHeaderView::section { background: #15151e; color: #a1a1a5; border: none; border-right: 1px solid #1e1e28; border-bottom: 1px solid #1e1e28; padding: 8px 4px; }
        QTableWidget#isaTable::item { padding: 6px; }
        QHeaderView::section { background: #141923; color: #9eafc3; border: none; padding: 6px; }
        QPushButton#settingsNavButton { background: #15151e; border: 1px solid #1e1e28; border-radius: 6px; color: #a1a1a5; }
        QPushButton#settingsNavButton:hover { color: #d9ecff; border-color: #2588c8; }
        QPushButton#settingsNavButton:checked { background: #0a8cfe; background-image: url(:/button/primary_bg.png); border-color: #0a8cfe; color: #d6ecff; }
        QPlainTextEdit, QTextEdit { background: #080d18; border: 1px solid #202938; color: #b9c9d9; border-radius: 8px; padding: 10px; }
        QSplitter::handle { background: #1e1e28; height: 3px; }
        QWidget#statusBarWidget { background: #1a1a28; border-top: 1px solid rgba(255,255,255,40); }
        QStatusBar { background: #1a1a28; color: #606d79; }
    )"));
}

void MainWindow::buildUi()
{
    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    buildTitleBar();
    root->addWidget(m_titleBar);

    m_pages = new QStackedWidget(central);
    buildMonitorPage();
    m_playbackPage = new PlaybackPage(m_pages);
    m_settingsPage = new SettingsPage(m_pages);
    m_pages->addWidget(m_monitorPage);
    m_pages->addWidget(m_playbackPage);
    m_pages->addWidget(m_settingsPage);
    root->addWidget(m_pages, 1);
    setCentralWidget(central);
    statusBar()->setSizeGripEnabled(false);
    statusBar()->setVisible(false);
    statusBar()->showMessage(QStringLiteral("系统已就绪"));
}

void MainWindow::buildTitleBar()
{
    m_titleBar = new QFrame(this);
    m_titleBar->setObjectName(QStringLiteral("titleBar"));
    m_titleBar->setFixedHeight(50);
    m_titleBar->setMouseTracking(true);
    m_titleBar->installEventFilter(this);
    auto* layout = new QHBoxLayout(m_titleBar);
    layout->setContentsMargins(12, 0, 0, 0);
    layout->setSpacing(0);

    auto* logo = new QLabel(m_titleBar);
    logo->setAttribute(Qt::WA_TransparentForMouseEvents);
    logo->setFixedSize(30, 30);
    logo->setPixmap(QPixmap(QStringLiteral(":/title/title_log.png"))
        .scaled(30, 30, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    layout->addWidget(logo);
    layout->addSpacing(6);
    auto* title = label(QStringLiteral("智能频谱监测仪"), m_titleBar, QStringLiteral("appTitle"));
    title->setAttribute(Qt::WA_TransparentForMouseEvents);
    layout->addWidget(title);
    layout->addSpacing(20);

    auto makeNavIcon = [](const QString& normalPath, const QString& hoverPath) {
        QIcon icon;
        icon.addPixmap(QPixmap(normalPath), QIcon::Normal, QIcon::Off);
        icon.addPixmap(QPixmap(hoverPath), QIcon::Active, QIcon::Off);
        icon.addPixmap(QPixmap(hoverPath), QIcon::Selected, QIcon::Off);
        return icon;
    };
    m_monitorTab = headerButton(QStringLiteral("采集监测"), m_titleBar);
    m_monitorTab->setIcon(makeNavIcon(QStringLiteral(":/title/collect_btn.png"),
                                       QStringLiteral(":/title/collect_btn_mouseOver.png")));
    m_playbackTab = headerButton(QStringLiteral("录制回放"), m_titleBar);
    m_playbackTab->setIcon(makeNavIcon(QStringLiteral(":/title/playback.png"),
                                        QStringLiteral(":/title/playback_mouseOver.png")));
    m_settingsTab = headerButton(QStringLiteral("系统设置"), m_titleBar);
    m_settingsTab->setIcon(makeNavIcon(QStringLiteral(":/title/setting.png"),
                                        QStringLiteral(":/title/setting_mouseOver.png")));
    layout->addWidget(m_monitorTab);
    layout->addWidget(m_playbackTab);
    layout->addWidget(m_settingsTab);
    layout->addStretch();

    m_minimizeButton = windowButton(QString(), m_titleBar);
    m_minimizeButton->setIcon(QIcon(QStringLiteral(":/title/min.png")));
    m_minimizeButton->setIconSize(QSize(16, 16));
    m_maximizeButton = windowButton(QString(), m_titleBar);
    m_maximizeButton->setIcon(QIcon(QStringLiteral(":/title/max.png")));
    m_maximizeButton->setIconSize(QSize(16, 16));
    m_closeButton = windowButton(QString(), m_titleBar);
    m_closeButton->setObjectName(QStringLiteral("closeWindowButton"));
    m_closeButton->setIcon(QIcon(QStringLiteral(":/title/close.png")));
    m_closeButton->setIconSize(QSize(16, 16));
    layout->addWidget(m_minimizeButton);
    layout->addWidget(m_maximizeButton);
    layout->addWidget(m_closeButton);

    m_monitorTab->setChecked(true);
    connect(m_monitorTab, &QPushButton::clicked, this, [this] { selectMainPage(0); });
    connect(m_playbackTab, &QPushButton::clicked, this, [this] { selectMainPage(1); });
    connect(m_settingsTab, &QPushButton::clicked, this, [this] { selectMainPage(2); });
    connect(m_minimizeButton, &QPushButton::clicked, this, &QWidget::showMinimized);
    connect(m_maximizeButton, &QPushButton::clicked, this, &MainWindow::toggleMaximize);
    connect(m_closeButton, &QPushButton::clicked, this, &QWidget::close);
}

void MainWindow::buildMonitorPage()
{
    m_monitorPage = new QWidget(m_pages);
    auto* root = new QVBoxLayout(m_monitorPage);
    root->setContentsMargins(12, 10, 12, 8);
    root->setSpacing(8);
    buildMonitorControlPanel(m_monitorPage);

    auto* plotPanel = new QFrame(m_monitorPage);
    plotPanel->setObjectName(QStringLiteral("displayPanel"));
    auto* plotLayout = new QVBoxLayout(plotPanel);
    plotLayout->setContentsMargins(1, 1, 1, 1);
    plotLayout->setSpacing(0);
    m_plotSplitter = new QSplitter(Qt::Vertical, plotPanel);
    auto* splitter = m_plotSplitter;
    m_frequencyNavigator = new FrequencyNavigatorWidget(plotPanel);
    m_waterfall = new WaterfallWidget(splitter);
    m_spectrum = new SpectrumWidget(splitter);
    m_waterfall->setMinimumHeight(135);
    m_spectrum->setMinimumHeight(280);
    splitter->addWidget(m_waterfall);
    splitter->addWidget(m_spectrum);
    plotLayout->addWidget(m_frequencyNavigator, 0);
    plotLayout->addWidget(splitter, 1);
    splitter->setCollapsible(0, false);
    splitter->setCollapsible(1, false);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 5);
    splitter->setSizes({250, 500});
    root->addWidget(plotPanel, 1);

    buildSignalTable(m_monitorPage);
    root->addWidget(m_statusStrip);
}

void MainWindow::buildMonitorControlPanel(QWidget* parent)
{
    auto* panel = new QFrame(parent);
    panel->setObjectName(QStringLiteral("controlPanel"));
    panel->setMinimumHeight(112);
    auto* layout = new QHBoxLayout(panel);
    layout->setContentsMargins(14, 8, 14, 8);
    layout->setSpacing(16);

    auto* left = new QWidget(panel);
    auto* configGrid = new QGridLayout(left);
    configGrid->setContentsMargins(0, 0, 0, 0);
    configGrid->setHorizontalSpacing(10);
    configGrid->setVerticalSpacing(6);

    auto* sourceLabel = label(QStringLiteral("数据源"), left, QStringLiteral("controlLabel"));
    sourceLabel->setMinimumWidth(52);
    configGrid->addWidget(sourceLabel, 0, 0);
    m_sourceCombo = new QComboBox(left);
    m_sourceCombo->addItem(QStringLiteral("BB60C"), static_cast<int>(algorithm::SourceKind::BB60C));
    m_sourceCombo->addItem(QStringLiteral("Harogic"), static_cast<int>(algorithm::SourceKind::Harogic));
    m_sourceCombo->addItem(QStringLiteral("FILE"), static_cast<int>(algorithm::SourceKind::File));
    m_sourceCombo->setMinimumWidth(96);
    configGrid->addWidget(m_sourceCombo, 0, 1);

    const auto parameterGroup = [left](const QString& title, QWidget* field) {
        auto* group = new QWidget(left);
        auto* groupLayout = new QHBoxLayout(group);
        groupLayout->setContentsMargins(0, 0, 0, 0);
        groupLayout->setSpacing(8);
        auto* titleLabel = label(title, group, QStringLiteral("controlLabel"));
        titleLabel->setMinimumWidth(52);
        groupLayout->addWidget(titleLabel);
        groupLayout->addWidget(field, 1);
        return group;
    };

    m_centerFrequency = new FrequencySpinBox(left);
    m_centerFrequency->setRange(0.0, 6.4e9);
    m_centerFrequency->setFrequencyHz(2400000000LL);
    m_centerFrequency->setMinimumWidth(150);
    m_centerGroup = parameterGroup(QStringLiteral("中心频率"), m_centerFrequency);
    configGrid->addWidget(m_centerGroup, 0, 2, 1, 2);

    m_bandwidth = new FrequencySpinBox(left);
    m_bandwidth->setRange(20.0, 6.4e9);
    m_bandwidth->setFrequencyHz(100000000LL);
    m_bandwidth->setMinimumWidth(150);
    m_bandwidthGroup = parameterGroup(QStringLiteral("扫宽"), m_bandwidth);
    configGrid->addWidget(m_bandwidthGroup, 0, 4, 1, 2);

    auto* fileGroup = new QWidget(left);
    auto* fileLayout = new QHBoxLayout(fileGroup);
    fileLayout->setContentsMargins(0, 0, 0, 0);
    fileLayout->setSpacing(8);
    auto* fileLabel = label(QStringLiteral("文件路径"), fileGroup, QStringLiteral("controlLabel"));
    fileLabel->setMinimumWidth(52);
    fileLayout->addWidget(fileLabel);
    m_filePath = new QLineEdit(fileGroup);
    m_filePath->setPlaceholderText(QStringLiteral("请选择回放文件..."));
    m_filePath->setMinimumWidth(300);
    m_filePath->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    fileLayout->addWidget(m_filePath, 1);
    m_browseButton = new QPushButton(fileGroup);
    m_browseButton->setIcon(QIcon(QStringLiteral(":/button/folder.png")));
    m_browseButton->setIconSize(QSize(16, 16));
    m_browseButton->setObjectName(QStringLiteral("pageToolButton"));
    m_browseButton->setToolTip(QStringLiteral("选择频谱文件"));
    m_browseButton->setFixedSize(34, 30);
    fileLayout->addWidget(m_browseButton);
    m_fileSourceGroup = fileGroup;
    configGrid->addWidget(m_fileSourceGroup, 0, 2, 1, 6);
    m_fileSourceGroup->setVisible(false);

    m_resolutionBandwidth = new FrequencySpinBox(left);
    m_resolutionBandwidth->setRange(1.0, 10.1e6);
    m_resolutionBandwidth->setFrequencyHz(50000LL);
    m_resolutionBandwidth->setMinimumWidth(130);
    m_resolutionBandwidthGroup = parameterGroup(QStringLiteral("带宽分辨率"), m_resolutionBandwidth);
    configGrid->addWidget(m_resolutionBandwidthGroup, 0, 6, 1, 2);

    // ISA keeps start/stop frequency visible in both live and file modes.
    // These controls use Hz internally and accept an explicit unit suffix.
    m_startFrequency = new FrequencySpinBox(left);
    m_startFrequency->setRange(0.0, 6.4e9);
    m_startFrequency->setFrequencyHz(2350000000LL);
    m_startFrequency->setMinimumWidth(150);
    auto* startGroup = parameterGroup(QStringLiteral("起始频率"), m_startFrequency);
    configGrid->addWidget(startGroup, 1, 0, 1, 2);

    m_endFrequency = new FrequencySpinBox(left);
    m_endFrequency->setRange(0.0, 6.4e9);
    m_endFrequency->setFrequencyHz(2450000000LL);
    m_endFrequency->setMinimumWidth(150);
    auto* endGroup = parameterGroup(QStringLiteral("终止频率"), m_endFrequency);
    configGrid->addWidget(endGroup, 1, 2, 1, 2);

    m_referenceLevel = new QDoubleSpinBox(left);
    m_referenceLevel->setRange(-1000.0, 1000.0);
    m_referenceLevel->setDecimals(1);
    m_referenceLevel->setValue(-25.0);
    m_referenceLevel->setSuffix(QStringLiteral(" dBm"));
    m_referenceLevel->setMinimumWidth(120);
    m_referenceLevel->setToolTip(QStringLiteral(
        "用于自动增益/衰减控制；频谱图和瀑布图动态范围可在系统设置的显示页调整。"));
    auto* referenceGroup = parameterGroup(QStringLiteral("参考电平"), m_referenceLevel);
    configGrid->addWidget(referenceGroup, 1, 4, 1, 2);

    m_rbwShape = new QComboBox(left);
    m_rbwShape->addItem(QStringLiteral("Nuttall"), static_cast<int>(source::RbwShape::Nuttall));
    m_rbwShape->addItem(QStringLiteral("Flattop"), static_cast<int>(source::RbwShape::Flattop));
    m_rbwShape->addItem(QStringLiteral("CISPR"), static_cast<int>(source::RbwShape::Cispr));
    m_rbwShape->setToolTip(QStringLiteral(
        "RBW窗口：Nuttall速度优先，Flattop幅度精度优先，CISPR为6 dB截止。实时监测中更改后会重新配置数据源。"));
    m_rbwShape->setMinimumWidth(130);
    m_rbwShapeGroup = parameterGroup(QStringLiteral("RBW窗口"), m_rbwShape);
    configGrid->addWidget(m_rbwShapeGroup, 1, 6, 1, 2);
    const QString liveParameterHint = QStringLiteral(
        "实时监测期间可修改；按 Enter 或离开输入框后会重新配置实时源，并从新参数重新累计检测结果。");
    m_centerFrequency->setToolTip(liveParameterHint);
    m_bandwidth->setToolTip(liveParameterHint);
    m_startFrequency->setToolTip(liveParameterHint);
    m_endFrequency->setToolTip(liveParameterHint);
    m_resolutionBandwidth->setToolTip(liveParameterHint);
    m_referenceLevel->setToolTip(QStringLiteral(
        "用于硬件增益/衰减控制；实时监测期间按 Enter 或离开输入框后应用。频谱图和瀑布图动态范围可在系统设置的显示页调整。"));
    configGrid->setColumnStretch(1, 1);
    configGrid->setColumnStretch(3, 1);
    configGrid->setColumnStretch(5, 1);
    configGrid->setColumnStretch(7, 1);
    layout->addWidget(left, 1);

    auto addDivider = [panel, layout]() {
        auto* divider = new QFrame(panel);
        divider->setObjectName(QStringLiteral("controlDivider"));
        divider->setFrameShape(QFrame::VLine);
        divider->setFrameShadow(QFrame::Plain);
        divider->setFixedWidth(1);
        layout->addWidget(divider);
    };

    addDivider();

    // Middle block: lifecycle operations remain visually separate from the
    // parameter grid and the alarm statistics.
    auto* operationPanel = new QWidget(panel);
    operationPanel->setMinimumWidth(300);
    operationPanel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    auto* operationLayout = new QVBoxLayout(operationPanel);
    operationLayout->setContentsMargins(0, 0, 0, 0);
    operationLayout->setSpacing(8);
    operationLayout->addStretch(1);

    m_applyButton = new QPushButton(QStringLiteral("应用参数"), panel);
    m_applyButton->setObjectName(QStringLiteral("pageToolButton"));
    m_applyButton->setVisible(false);
    auto* operation = new QHBoxLayout;
    operation->setContentsMargins(0, 0, 0, 0);
    operation->setSpacing(8);
    m_startButton = new QPushButton(QStringLiteral("开始监测"), panel);
    m_startButton->setObjectName(QStringLiteral("primaryButton"));
    m_startButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    m_startButton->setMinimumHeight(44);
    m_startButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    operation->addWidget(m_startButton, 1);
    m_pauseButton = new QPushButton(QStringLiteral("暂停查看"), panel);
    m_pauseButton->setObjectName(QStringLiteral("dangerButton"));
    m_pauseButton->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
    m_pauseButton->setMinimumHeight(44);
    m_pauseButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_pauseButton->setEnabled(false);
    operation->addWidget(m_pauseButton, 1);
    operationLayout->addLayout(operation);
    operationLayout->addStretch(1);
    layout->addWidget(operationPanel, 0);

    addDivider();

    // Right block: three equal statistic cards form one aligned status area.
    auto* metricsPanel = new QWidget(panel);
    metricsPanel->setMinimumWidth(430);
    metricsPanel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    auto* metrics = new QHBoxLayout(metricsPanel);
    metrics->setContentsMargins(0, 0, 0, 0);
    metrics->setSpacing(8);
    auto* critical = metricCard(QStringLiteral(":/collect/critical_alert.png"), QStringLiteral("严重警告"),
                                QStringLiteral("#E63E3E"), m_criticalAlertLabel, metricsPanel);
    auto* general = metricCard(QStringLiteral(":/collect/general_alarm.png"), QStringLiteral("一般警告"),
                               QStringLiteral("#FFBA00"), m_generalAlarmLabel, metricsPanel);
    auto* total = metricCard(QStringLiteral(":/collect/signal_total.png"), QStringLiteral("信号总数"),
                             QStringLiteral("#0A8CFE"), m_signalTotalLabel, metricsPanel);
    m_criticalAlertLabel->setText(QStringLiteral("0"));
    m_generalAlarmLabel->setText(QStringLiteral("0"));
    critical->setToolTip(QStringLiteral("当前活动的严重告警事件数。告警等级由规则决定，不由 SCN 置信度自动映射。"));
    general->setToolTip(QStringLiteral("当前活动的一般告警事件数。"));
    total->setToolTip(QStringLiteral("最新检测结果中的观测信号数。"));
    metrics->addWidget(critical, 1);
    metrics->addWidget(general, 1);
    metrics->addWidget(total, 1);
    layout->addWidget(metricsPanel, 0);

    m_stateLabel = label(QStringLiteral("已停止"), panel, QStringLiteral("stateLabel"));
    m_stateLabel->setVisible(false);
    m_frameLabel = label(QStringLiteral("帧号：-"), panel, QStringLiteral("frameLabel"));
    m_frameLabel->setVisible(false);

    m_pointCount = new QSpinBox(panel);
    m_pointCount->setRange(64, 10000000);
    m_pointCount->setValue(2048);
    m_frameRate = new QSpinBox(panel);
    m_frameRate->setRange(1, 120);
    m_frameRate->setValue(30);
    m_frameRate->setSuffix(QStringLiteral(" fps"));
    m_loopFile = new QCheckBox(QStringLiteral("循环读取文件"), panel);
    m_loopFile->setChecked(true);
    m_pointCount->setVisible(false);
    m_frameRate->setVisible(false);
    m_loopFile->setVisible(false);
    auto* outer = qobject_cast<QVBoxLayout*>(parent->layout());
    if (outer) outer->addWidget(panel);

    connect(m_browseButton, &QPushButton::clicked, this, &MainWindow::browseFile);
    connect(m_sourceCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &MainWindow::sourceSelectionChanged);
    connect(m_applyButton, &QPushButton::clicked, this, &MainWindow::applyConfiguration);
    connect(m_startButton, &QPushButton::clicked, this, &MainWindow::startMonitoring);
    connect(m_pauseButton, &QPushButton::clicked, this, &MainWindow::pauseMonitoring);

    const auto applyLiveConfiguration = [this] {
        QTimer::singleShot(0, this, &MainWindow::applyLiveSourceConfiguration);
    };
    for (auto* editor : {static_cast<QAbstractSpinBox*>(m_centerFrequency),
                         static_cast<QAbstractSpinBox*>(m_bandwidth),
                         static_cast<QAbstractSpinBox*>(m_startFrequency),
                         static_cast<QAbstractSpinBox*>(m_endFrequency),
                         static_cast<QAbstractSpinBox*>(m_resolutionBandwidth),
                         static_cast<QAbstractSpinBox*>(m_referenceLevel)}) {
        connect(editor, &QAbstractSpinBox::editingFinished, this, applyLiveConfiguration);
    }
    connect(m_rbwShape, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [applyLiveConfiguration](int) { applyLiveConfiguration(); });

    const auto syncFromCenterSpan = [this](double) {
        if (m_updatingFrequency) return;
        m_updatingFrequency = true;
        const auto centerHz = m_centerFrequency->frequencyHz();
        const auto spanHz = m_bandwidth->frequencyHz();
        const auto startHz = centerHz - spanHz / 2;
        m_startFrequency->setFrequencyHz(startHz);
        m_endFrequency->setFrequencyHz(startHz + spanHz);
        m_updatingFrequency = false;
    };
    const auto syncFromStartEnd = [this](double) {
        if (m_updatingFrequency) return;
        m_updatingFrequency = true;
        const auto startHz = m_startFrequency->frequencyHz();
        const auto endHz = m_endFrequency->frequencyHz();
        const auto spanHz = endHz - startHz;
        m_centerFrequency->setFrequencyHz(startHz + spanHz / 2);
        m_bandwidth->setFrequencyHz(std::max<std::int64_t>(20, spanHz));
        m_updatingFrequency = false;
    };
    connect(m_centerFrequency, &QDoubleSpinBox::valueChanged, this, syncFromCenterSpan);
    connect(m_bandwidth, &QDoubleSpinBox::valueChanged, this, syncFromCenterSpan);
    connect(m_startFrequency, &QDoubleSpinBox::valueChanged, this, syncFromStartEnd);
    connect(m_endFrequency, &QDoubleSpinBox::valueChanged, this, syncFromStartEnd);

    connect(m_startFrequency, &QDoubleSpinBox::valueChanged,
            this, [this](double) { updateDisplayDomain(); });
    connect(m_endFrequency, &QDoubleSpinBox::valueChanged,
            this, [this](double) { updateDisplayDomain(); });
    connect(m_referenceLevel, &QDoubleSpinBox::valueChanged,
            this, [this](double) { updateDisplayDomain(); });
}

void MainWindow::buildSignalTable(QWidget* parent)
{
    auto* panel = new QFrame(parent);
    panel->setObjectName(QStringLiteral("signalPanel"));
    panel->setMinimumHeight(218);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(12, 2, 12, 2);

    m_detectionStatusLabel = new QLabel(QStringLiteral("SCN：等待初始化"), panel);
    m_detectionStatusLabel->setTextFormat(Qt::PlainText);
    m_detectionStatusLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_detectionStatusLabel->setMinimumWidth(0);
    layout->addWidget(m_detectionStatusLabel);
    m_signalTable = new QTableWidget(0, 7, panel);
    m_signalTable->setObjectName(QStringLiteral("isaTable"));
    m_signalTable->setHorizontalHeaderLabels({QStringLiteral("ID"), QStringLiteral("中心频率"),
        QStringLiteral("带宽"), QStringLiteral("信号类型"), QStringLiteral("告警等级"),
        QStringLiteral("最近出现时间"), QStringLiteral("出现次数")});
    m_signalTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_signalTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_signalTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_signalTable->verticalHeader()->setDefaultSectionSize(30);
    layout->addWidget(m_signalTable);
    auto* outer = qobject_cast<QVBoxLayout*>(parent->layout());
    if (outer) outer->addWidget(panel, 0);
    connect(m_signalTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (!m_signalTable || row < 0) return;
        const auto* item = m_signalTable->item(row, 0);
        if (!item) return;
        QMessageBox::information(this, QStringLiteral("信号详情"),
                                 item->data(Qt::UserRole).toString());
    });

    m_statusStrip = new StatusBarWidget(parent);
}

void MainWindow::buildMenus()
{
    auto* aboutAction = new QAction(QStringLiteral("关于智能频谱监测仪"), this);
    connect(aboutAction, &QAction::triggered, this, &MainWindow::showAbout);
    addAction(aboutAction);
    setContextMenuPolicy(Qt::ActionsContextMenu);
}

source::SourceConfig MainWindow::currentConfig() const
{
    source::SourceConfig config;
    config.kind = static_cast<algorithm::SourceKind>(m_sourceCombo->currentData().toInt());
    config.filePath = m_filePath->text().trimmed().toStdString();
    if (config.kind == algorithm::SourceKind::File) {
        const auto startHz = m_startFrequency->frequencyHz();
        const auto endHz = m_endFrequency->frequencyHz();
        const auto spanHz = endHz - startHz;
        config.centerFrequencyHz = startHz + spanHz / 2;
        config.bandwidthHz = endHz - startHz;
    } else {
        config.centerFrequencyHz = m_centerFrequency->frequencyHz();
        config.bandwidthHz = std::max<std::int64_t>(20, m_bandwidth->frequencyHz());
    }
    config.resolutionBandwidthHz = m_resolutionBandwidth->frequencyHz();
    config.referenceLevelDbm = m_referenceLevel->value();
    config.rbwShape = static_cast<source::RbwShape>(m_rbwShape->currentData().toInt());
    config.pointCount = static_cast<std::size_t>(m_pointCount->value());
    config.frameRateHz = m_frameRate->value();
    config.loopFile = m_loopFile->isChecked();
    return config;
}

application::RecordingConfig MainWindow::recordingConfig() const
{
    auto config = m_settingsPage ? m_settingsPage->recordingConfig()
                                  : application::RecordingConfig{};
    if (m_sourceCombo && m_sourceCombo->currentData().toInt() ==
        static_cast<int>(algorithm::SourceKind::File)) {
        config.enabled = false;
    }
    return config;
}

QString MainWindow::formatFrequency(double hz, int decimals) const
{
    Q_UNUSED(decimals)
    return FrequencySpinBox::formatFrequency(hz);
}

bool MainWindow::validateConfiguration(const source::SourceConfig& config, QString& error) const
{
    if (config.kind == algorithm::SourceKind::File) {
        if (config.filePath.empty()) {
            error = QStringLiteral("请选择频谱文件。");
            return false;
        }
        if (config.bandwidthHz <= 0) {
            error = QStringLiteral("文件源的起始频率必须小于终止频率。");
            return false;
        }
        return true;
    }

    const double startHz = static_cast<double>(config.centerFrequencyHz) -
        static_cast<double>(config.bandwidthHz) / 2.0;
    const double endHz = static_cast<double>(config.centerFrequencyHz) +
        static_cast<double>(config.bandwidthHz) / 2.0;
    if (!std::isfinite(startHz) || !std::isfinite(endHz) ||
        startHz < 9.0e3 || endHz > 6.0e9 || endHz <= startHz) {
        error = QStringLiteral("起止频率必须位于 9 kHz 至 6 GHz 范围内。");
        return false;
    }
    if (config.bandwidthHz < 20) {
        error = QStringLiteral("扫宽不能小于 20 Hz。");
        return false;
    }
    if (config.resolutionBandwidthHz < 1 ||
        config.resolutionBandwidthHz > 10.1e6) {
        error = QStringLiteral("RBW 必须位于 1 Hz 至 10.1 MHz 范围内。");
        return false;
    }
    if (!std::isfinite(config.referenceLevelDbm) || config.referenceLevelDbm > 20.0) {
        error = QStringLiteral("BB60C 参考电平不能大于 20 dBm。");
        return false;
    }
    if (config.rbwShape < source::RbwShape::Nuttall ||
        config.rbwShape > source::RbwShape::Cispr) {
        error = QStringLiteral("RBW窗口类型无效。");
        return false;
    }
    return true;
}

void MainWindow::browseFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择频谱文件"), QString(),
        QStringLiteral("Spectrum files (*.bin *.dat *.txt *.csv *.asc);;All files (*.*)"));
    if (path.isEmpty()) return;
    m_filePath->setText(path);
    clearMonitoringDisplay();
    if (m_sourceCombo->currentData().toInt() != static_cast<int>(algorithm::SourceKind::File)) {
        m_sourceCombo->setCurrentIndex(2);
    } else {
        applyFileMetadata(path);
    }
}

void MainWindow::applyFileMetadata(const QString& path)
{
    m_fileFrequencyMetadataLocked = false;
    m_fileRbwMetadataLocked = false;
    m_fileReferenceMetadataLocked = false;
    source::FileSourceMetadata metadata;
    std::string error;
    if (!source::FileSource::inspectFile(path.toStdString(), metadata, error)) {
        statusBar()->showMessage(QString::fromStdString(error), 6000);
        updateSourceControls();
        updateDisplayDomain();
        return;
    }

    if (metadata.hasCenterFrequency && metadata.hasBandwidth) {
        m_updatingFrequency = true;
        const auto startHz = metadata.centerFrequencyHz - metadata.bandwidthHz / 2;
        m_startFrequency->setFrequencyHz(startHz);
        m_endFrequency->setFrequencyHz(startHz + metadata.bandwidthHz);
        m_centerFrequency->setFrequencyHz(metadata.centerFrequencyHz);
        m_bandwidth->setFrequencyHz(metadata.bandwidthHz);
        m_updatingFrequency = false;
        m_fileFrequencyMetadataLocked = true;
    }
    if (metadata.hasResolutionBandwidth) {
        m_resolutionBandwidth->setFrequencyHz(metadata.resolutionBandwidthHz);
        m_fileRbwMetadataLocked = true;
    }
    if (metadata.hasReferenceLevel) {
        m_referenceLevel->setValue(metadata.referenceLevelDbm);
        m_fileReferenceMetadataLocked = true;
    }
    if (metadata.hasSpectrumLength) {
        const auto length = static_cast<qlonglong>(metadata.spectrumLength);
        m_pointCount->setMaximum(static_cast<int>(std::min<qlonglong>(length, 10000000)));
        m_pointCount->setValue(static_cast<int>(std::min<qlonglong>(length, 10000000)));
    }
    QString message = QStringLiteral("已读取原 ISA 文件参数：%1")
        .arg(QFileInfo(path).fileName());
    if (metadata.isBinary && metadata.hasSpectrumLength) {
        message += QStringLiteral("（float32，完整帧 %1，尾部 %2 字节）")
            .arg(static_cast<qulonglong>(metadata.completeFrameCount))
            .arg(static_cast<qulonglong>(metadata.trailingBytes));
    }
    statusBar()->showMessage(message, 5000);
    updateSourceControls();
    updateDisplayDomain();
}

void MainWindow::sourceSelectionChanged(int)
{
    const bool fileSource = m_sourceCombo->currentData().toInt() == static_cast<int>(algorithm::SourceKind::File);
    if (fileSource) {
        const QString path = m_filePath->text().trimmed();
        if (!path.isEmpty() && QFileInfo::exists(path)) applyFileMetadata(path);
    }
    updateSourceControls();
    clearMonitoringDisplay();
    updateDisplayDomain();
    if (m_sourceCombo && m_statusStrip) {
        m_statusStrip->setDeviceValue(m_sourceCombo->currentText());
        m_statusStrip->setMenuInfoVisible(fileSource);
    }
    m_missingLiveSourceWarningShown = false;
    updateStartButtonAvailability();
    if (!fileSource) warnForMissingSelectedLiveSource();
}

void MainWindow::updateSourceControls()
{
    if (!m_sourceCombo) return;
    const bool fileSource = m_sourceCombo->currentData().toInt() == static_cast<int>(algorithm::SourceKind::File);
    const bool sourceSelectionEditable = !m_monitoring;
    const bool liveParametersEditable = !fileSource;
    m_centerFrequency->setEnabled(liveParametersEditable);
    m_bandwidth->setEnabled(liveParametersEditable);
    if (m_centerGroup) m_centerGroup->setVisible(!fileSource);
    if (m_bandwidthGroup) m_bandwidthGroup->setVisible(!fileSource);
    if (m_fileSourceGroup) m_fileSourceGroup->setVisible(fileSource);
    m_filePath->setEnabled(fileSource && sourceSelectionEditable);
    m_browseButton->setEnabled(fileSource && sourceSelectionEditable);
    // FILE metadata are authoritative for the active file.  Keep the values
    // visible for ISA-style inspection, but never allow editing them in FILE
    // mode; a different file is selected through the Browse button.
    m_startFrequency->setEnabled(liveParametersEditable);
    m_endFrequency->setEnabled(liveParametersEditable);
    m_resolutionBandwidth->setEnabled(liveParametersEditable);
    m_referenceLevel->setEnabled(liveParametersEditable);
    m_rbwShape->setEnabled(liveParametersEditable);
    if (m_rbwShapeGroup) m_rbwShapeGroup->setVisible(!fileSource);
    if (auto* grid = m_centerGroup
                         ? qobject_cast<QGridLayout*>(m_centerGroup->parentWidget()->layout())
                         : nullptr) {
        if (m_resolutionBandwidthGroup) {
            grid->addWidget(m_resolutionBandwidthGroup, fileSource ? 1 : 0, 6, 1, 2);
            m_resolutionBandwidthGroup->setVisible(true);
        }
        grid->invalidate();
        grid->activate();
    }
    m_loopFile->setEnabled(fileSource && sourceSelectionEditable);
    m_sourceCombo->setEnabled(sourceSelectionEditable);
    m_pointCount->setEnabled(liveParametersEditable);
    m_frameRate->setEnabled(liveParametersEditable);
    m_applyButton->setEnabled(sourceSelectionEditable);
    updateStartButtonAvailability();
}

bool MainWindow::liveSourcePresence(algorithm::SourceKind kind, bool* known) const
{
    if (kind == algorithm::SourceKind::BB60C) {
        if (known) *known = m_bb60cPresenceKnown;
        return m_bb60cConnected;
    }
    if (kind == algorithm::SourceKind::Harogic) {
        if (known) *known = m_harogicPresenceKnown;
        return m_harogicConnected;
    }
    if (known) *known = true;
    return true;
}

void MainWindow::updateStartButtonAvailability()
{
    if (!m_startButton || !m_sourceCombo) return;
    if (m_monitoring) {
        m_startButton->setEnabled(true); // Keep Stop available even if the device disappears.
        m_startButton->setToolTip(QString());
        return;
    }
    const auto kind = static_cast<algorithm::SourceKind>(m_sourceCombo->currentData().toInt());
    bool known = false;
    const bool present = liveSourcePresence(kind, &known);
    const bool enabled = kind == algorithm::SourceKind::File || (known && present);
    m_startButton->setEnabled(enabled);
    m_startButton->setToolTip(enabled ? QString() :
        QStringLiteral("所选实时源尚未接入，检测到设备后才能开始监测。"));
}

void MainWindow::warnForMissingSelectedLiveSource()
{
    if (m_missingLiveSourceWarningShown || !m_sourceCombo) return;
    const auto kind = static_cast<algorithm::SourceKind>(m_sourceCombo->currentData().toInt());
    if (kind == algorithm::SourceKind::File) return;
    bool known = false;
    if (liveSourcePresence(kind, &known) || !known) return;

    m_missingLiveSourceWarningShown = true;
    const QString sourceName = kind == algorithm::SourceKind::BB60C
        ? QStringLiteral("BB60C") : QStringLiteral("海得罗捷");
    QMessageBox::warning(this, QStringLiteral("实时源未接入"),
        QStringLiteral("当前选择的 %1 设备未接入。请连接设备后再开始监测。")
            .arg(sourceName));
}

void MainWindow::updateDisplayDomain()
{
    if (!m_spectrum || !m_waterfall || !m_startFrequency || !m_endFrequency ||
        !m_referenceLevel) {
        return;
    }
    const double startHz = static_cast<double>(m_startFrequency->frequencyHz());
    const double endHz = static_cast<double>(m_endFrequency->frequencyHz());
    const double referenceLevelDbm = m_referenceLevel->value();
    if (!std::isfinite(startHz) || !std::isfinite(endHz) || !(endHz > startHz)) {
        return;
    }
    const double dynamicRangeDb = m_settingsPage
        ? m_settingsPage->displayDynamicRangeDb() : 80.0;
    m_spectrum->setDynamicRangeDb(dynamicRangeDb);
    m_waterfall->setDynamicRangeDb(dynamicRangeDb);
    m_spectrum->setDisplayDomain(startHz, endHz, referenceLevelDbm);
    m_waterfall->setDisplayDomain(startHz, endHz, referenceLevelDbm);
    syncFrequencyNavigator();
}

void MainWindow::syncFrequencyNavigator()
{
    if (!m_frequencyNavigator || !m_spectrum || !m_waterfall) return;
    double startHz = m_startFrequency ? static_cast<double>(m_startFrequency->frequencyHz()) : 0.0;
    double endHz = m_endFrequency ? static_cast<double>(m_endFrequency->frequencyHz()) : 0.0;
    if (m_displaySnapshot && m_displaySnapshot->frame.isValid()) {
        startHz = m_displaySnapshot->frame.startFrequencyHz;
        endHz = m_displaySnapshot->frame.endFrequencyHz();
    }
    if (!(std::isfinite(startHz) && std::isfinite(endHz) && endHz > startHz)) return;
    m_frequencyNavigator->setDomain(startHz, endHz);
    if (m_spectrum->hasFrequencyView()) {
        m_frequencyNavigator->setViewRange(m_spectrum->viewStartFrequency(),
                                           m_spectrum->viewEndFrequency());
    } else {
        m_frequencyNavigator->setViewRange(startHz, endHz);
    }
}

void MainWindow::loadUiState()
{
    QSettings settings(QStringLiteral("SCN"), QStringLiteral("HaiAISpecMonitor"));
    // Display zoom and selection are intentionally session-local.  Remove
    // legacy keys so older installations cannot restore them indirectly.
    settings.remove(QStringLiteral("display"));

    const int savedSourceKind = settings.value(
        QStringLiteral("source/kind"), static_cast<int>(algorithm::SourceKind::File)).toInt();
    int sourceIndex = m_sourceCombo->findData(savedSourceKind);
    if (sourceIndex < 0) {
        sourceIndex = m_sourceCombo->findData(static_cast<int>(algorithm::SourceKind::File));
    }
    m_sourceCombo->setCurrentIndex(sourceIndex);

    m_filePath->setText(settings.value(QStringLiteral("source/filePath"),
                                       defaultSpectrumFilePath()).toString());
    const auto readFrequency = [&settings](const QString& hzKey,
                                            const QString& legacyKey,
                                            std::int64_t fallback,
                                            double legacyScale) -> std::int64_t {
        std::int64_t exactHz = fallback;
        if (settings.contains(hzKey)) {
            return FrequencySpinBox::parseStoredFrequency(settings.value(hzKey), exactHz)
                ? exactHz : fallback;
        }

        const QVariant legacyValue = settings.value(
            legacyKey, static_cast<double>(fallback) / legacyScale);
        const double legacyHz = legacyValue.toDouble() * legacyScale;
        return scn::common::toIntegerHz(legacyHz, exactHz) ? exactHz : fallback;
    };
    m_centerFrequency->setFrequencyHz(readFrequency(QStringLiteral("source/centerFrequencyHz"),
                                               QStringLiteral("source/centerFrequencyGHz"),
                                               m_centerFrequency->frequencyHz(), 1.0e9));
    m_bandwidth->setFrequencyHz(readFrequency(QStringLiteral("source/bandwidthHz"),
                                        QStringLiteral("source/bandwidthGHz"),
                                        m_bandwidth->frequencyHz(), 1.0e9));
    m_startFrequency->setFrequencyHz(readFrequency(QStringLiteral("source/startFrequencyHz"),
                                             QStringLiteral("source/startFrequencyGHz"),
                                             m_startFrequency->frequencyHz(), 1.0e9));
    m_endFrequency->setFrequencyHz(readFrequency(QStringLiteral("source/endFrequencyHz"),
                                           QStringLiteral("source/endFrequencyGHz"),
                                           m_endFrequency->frequencyHz(), 1.0e9));
    m_resolutionBandwidth->setFrequencyHz(readFrequency(QStringLiteral("source/resolutionBandwidthHz"),
                                                  QStringLiteral("source/resolutionBandwidthKHz"),
                                                  m_resolutionBandwidth->frequencyHz(), 1.0e3));
    m_referenceLevel->setValue(settings.value(QStringLiteral("source/referenceLevelDbm"),
                                              m_referenceLevel->value()).toDouble());
    const int savedRbwShape = settings.value(QStringLiteral("source/rbwShape"),
                                              static_cast<int>(source::RbwShape::Nuttall)).toInt();
    const int rbwShapeIndex = m_rbwShape->findData(savedRbwShape);
    if (rbwShapeIndex >= 0) m_rbwShape->setCurrentIndex(rbwShapeIndex);
    m_pointCount->setValue(settings.value(QStringLiteral("source/pointCount"),
                                          m_pointCount->value()).toInt());
    m_frameRate->setValue(settings.value(QStringLiteral("source/frameRate"),
                                         m_frameRate->value()).toInt());
    m_loopFile->setChecked(settings.value(QStringLiteral("source/loopFile"),
                                          m_loopFile->isChecked()).toBool());
    m_spectrum->setMaxSpectrumVisible(
        settings.value(QStringLiteral("spectrum/showMaxSpectrum"), false).toBool());
    m_spectrum->setRealtimeSpectrumVisible(
        settings.value(QStringLiteral("spectrum/showRealtimeSpectrum"), true).toBool());
    m_spectrum->setAverageSpectrumVisible(
        settings.value(QStringLiteral("spectrum/showAverageSpectrum"), false).toBool());
    m_spectrum->setDetectionMarkersVisible(
        settings.value(QStringLiteral("spectrum/showDetectionMarkers"), true).toBool());

    const QString filePath = m_filePath->text().trimmed();
    if (savedSourceKind == static_cast<int>(algorithm::SourceKind::File) &&
        QFileInfo::exists(filePath)) {
        applyFileMetadata(filePath);
    }

    if (settings.contains(QStringLiteral("ui/geometry"))) {
        restoreGeometry(settings.value(QStringLiteral("ui/geometry")).toByteArray());
    }
    if (settings.contains(QStringLiteral("ui/windowState"))) {
        restoreState(settings.value(QStringLiteral("ui/windowState")).toByteArray(), 1);
    }
    if (m_plotSplitter && settings.contains(QStringLiteral("ui/plotSplitterState"))) {
        m_plotSplitter->restoreState(
            settings.value(QStringLiteral("ui/plotSplitterState")).toByteArray());
    }

    // Always start on the monitoring page.  The last top-level tab is a
    // transient navigation choice and is intentionally not persisted.
    settings.remove(QStringLiteral("ui/mainPage"));
    selectMainPage(0);
}

void MainWindow::saveUiState() const
{
    QSettings settings(QStringLiteral("SCN"), QStringLiteral("HaiAISpecMonitor"));
    settings.setValue(QStringLiteral("ui/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("ui/windowState"), saveState(1));
    // Do not persist the last top-level tab; startup always opens monitoring.
    settings.remove(QStringLiteral("ui/mainPage"));
    if (m_plotSplitter) {
        settings.setValue(QStringLiteral("ui/plotSplitterState"), m_plotSplitter->saveState());
    }

    const int sourceKind = m_sourceCombo->currentData().toInt();
    settings.setValue(QStringLiteral("source/kind"), sourceKind);
    settings.setValue(QStringLiteral("source/filePath"), m_filePath->text());
    settings.setValue(QStringLiteral("source/centerFrequencyHz"),
                      static_cast<qlonglong>(m_centerFrequency->frequencyHz()));
    settings.setValue(QStringLiteral("source/bandwidthHz"),
                      static_cast<qlonglong>(m_bandwidth->frequencyHz()));
    settings.setValue(QStringLiteral("source/startFrequencyHz"),
                      static_cast<qlonglong>(m_startFrequency->frequencyHz()));
    settings.setValue(QStringLiteral("source/endFrequencyHz"),
                      static_cast<qlonglong>(m_endFrequency->frequencyHz()));
    settings.setValue(QStringLiteral("source/resolutionBandwidthHz"),
                      static_cast<qlonglong>(m_resolutionBandwidth->frequencyHz()));
    settings.setValue(QStringLiteral("source/referenceLevelDbm"), m_referenceLevel->value());
    settings.setValue(QStringLiteral("source/rbwShape"), m_rbwShape->currentData().toInt());
    settings.setValue(QStringLiteral("source/pointCount"), m_pointCount->value());
    settings.setValue(QStringLiteral("source/frameRate"), m_frameRate->value());
    settings.setValue(QStringLiteral("source/loopFile"), m_loopFile->isChecked());
    if (m_settingsPage) m_settingsPage->saveRecordingConfig();
    settings.setValue(QStringLiteral("spectrum/showMaxSpectrum"),
                      m_spectrum->maxSpectrumVisible());
    settings.setValue(QStringLiteral("spectrum/showRealtimeSpectrum"),
                      m_spectrum->realtimeSpectrumVisible());
    settings.setValue(QStringLiteral("spectrum/showAverageSpectrum"),
                      m_spectrum->averageSpectrumVisible());
    settings.setValue(QStringLiteral("spectrum/showDetectionMarkers"),
                      m_spectrum->detectionMarkersVisible());
    settings.remove(QStringLiteral("display"));
    settings.sync();
}

void MainWindow::applyConfiguration()
{
    (void)applyCurrentConfiguration();
}

bool MainWindow::applyCurrentConfiguration()
{
    const auto config = currentConfig();
    if (config.kind != algorithm::SourceKind::File) {
        bool known = false;
        const bool connected = liveSourcePresence(config.kind, &known);
        if (!known || !connected) {
            statusBar()->showMessage(known ? QStringLiteral("实时设备未接入，不能应用实时源参数。")
                                           : QStringLiteral("正在检查实时设备接入状态，请稍候。"), 5000);
            if (known) warnForMissingSelectedLiveSource();
            updateStartButtonAvailability();
            return false;
        }
    }
    QString validationError;
    if (!validateConfiguration(config, validationError)) {
        statusBar()->showMessage(validationError, 6000);
        return false;
    }
    const bool liveReconfiguration = m_monitoring &&
        config.kind != algorithm::SourceKind::File;
    if (liveReconfiguration && m_hasLastAppliedSourceConfig &&
        sameSourceConfig(config, m_lastAppliedSourceConfig)) {
        return true;
    }
    // SCN controls are a draft. Only their explicit Apply action changes the
    // accepted detector configuration; source start must not commit that draft.
    // Session.configure creates a new generation synchronously. Reject already
    // published snapshots from the preceding run without waiting for Running.
    m_minimumGeneration = m_latestGeneration + 1;
    m_pendingSnapshot.reset();
    m_session.configureRecording(recordingConfig());
    m_controller.configure(config);
    m_lastAppliedSourceConfig = config;
    m_hasLastAppliedSourceConfig = true;
    updateDisplayDomain();
    m_spectrum->resetView();
    m_waterfall->resetView();
    m_statusStrip->setMenuInfo(config.centerFrequencyHz, config.bandwidthHz,
                               config.resolutionBandwidthHz);
    m_statusStrip->setMenuInfoVisible(config.kind == algorithm::SourceKind::File);
    statusBar()->showMessage(liveReconfiguration
        ? QStringLiteral("实时参数已提交，正在重新配置并恢复采集。")
        : QStringLiteral("参数已提交，等待数据源初始化。"), 4000);
    return true;
}

void MainWindow::applyLiveSourceConfiguration()
{
    if (!m_monitoring || !m_sourceCombo ||
        m_sourceCombo->currentData().toInt() == static_cast<int>(algorithm::SourceKind::File)) {
        return;
    }
    (void)applyCurrentConfiguration();
}

bool MainWindow::applyDetectionConfiguration()
{
    const auto config = m_settingsPage->detectionConfig();
    std::string error;
    if (!algorithm::validateConfig(config, error)) {
        m_settingsPage->setDetectionFeedback(QStringLiteral("SCN 设置未应用：%1")
            .arg(QString::fromStdString(error)));
        return false;
    }
    if (m_appliedDetectionConfig && algorithm::sameConfig(*m_appliedDetectionConfig, config)) {
        m_settingsPage->setDetectionFeedback(QStringLiteral("SCN 设置与当前配置一致。"));
        return true;
    }
    // Require the visible monitoring lifecycle to be stopped before asking for
    // a restart. EOF can make the worker inactive just before Stopped reaches
    // the UI; do not mistake an accepted restart for a rejected request then.
    if (m_monitoring && m_appliedDetectionConfig &&
        algorithm::classifyConfigChange(*m_appliedDetectionConfig, config) ==
            algorithm::ConfigApplyResult::RequiresRestart) {
        m_settingsPage->setDetectionFeedback(QStringLiteral(
            "SCN 设置未应用、未保存。请先停止监测，再更换模型、GPU 或切换检测开关。"));
        return false;
    }
    const auto result = m_session.configureDetection(config);
    if (result == algorithm::ConfigApplyResult::Invalid) {
        m_settingsPage->setDetectionFeedback(QStringLiteral("SCN 设置未应用：会话拒绝了此配置。"));
        return false;
    }
    if (result == algorithm::ConfigApplyResult::RequiresRestart && m_monitoring) {
        m_settingsPage->setDetectionFeedback(QStringLiteral(
            "SCN 设置未应用、未保存。请先停止监测，再更换模型、GPU 或切换检测开关。"));
        return false;
    }
    m_appliedDetectionConfig = config;
    m_settingsPage->acceptDetectionConfig(config);
    m_pendingSnapshot.reset();
    clearDetectionDisplay();
    m_detectionModelStatus = config.enabled
        ? QStringLiteral("SCN 配置已接收，等待检测状态。") : QStringLiteral("SCN 检测已关闭。");
    updateDetectionStatus(m_displaySnapshot.get());
    m_settingsPage->setDetectionFeedback(result == algorithm::ConfigApplyResult::RequiresReset
        ? QStringLiteral("SCN 设置已应用并保存，检测累计与跟踪将重置。")
        : QStringLiteral("SCN 设置已应用并保存。"));
    return true;
}

void MainWindow::clearDetectionDisplay()
{
    m_lastDetectionKey.reset();
    m_signalTable->setRowCount(0);
    m_criticalAlertLabel->setText(QStringLiteral("0"));
    m_generalAlarmLabel->setText(QStringLiteral("0"));
    m_signalTotalLabel->setText(QStringLiteral("0"));
    if (m_displaySnapshot) {
        auto snapshot = std::make_shared<algorithm::DisplaySnapshot>(*m_displaySnapshot);
        const auto generation = snapshot->detection.generation;
        const auto version = snapshot->detection.configVersion;
        snapshot->detection = {};
        snapshot->detection.generation = generation;
        snapshot->detection.configVersion = version;
        snapshot->detection.requiredFrames = m_appliedDetectionConfig
            ? m_appliedDetectionConfig->accumulator.frames : 16;
        m_displaySnapshot = snapshot;
        m_spectrum->setSnapshot(snapshot);
    }
}

void MainWindow::clearMonitoringDisplay(bool discardCurrentGeneration)
{
    if (discardCurrentGeneration) m_minimumGeneration = m_latestGeneration + 1;
    m_pendingSnapshot.reset();
    m_displaySnapshot.reset();
    m_policySnapshot.reset();
    m_pendingPolicySnapshot.reset();
    m_spectrum->setPolicySnapshot(nullptr);
    m_lastDetectionKey.reset();
    m_spectrum->clear();
    m_waterfall->clear();
    syncFrequencyNavigator();
    m_displayRateTimer.invalidate();
    m_displayRateFrames = 0;
    m_displayRateHz = 0;
    m_signalTable->setRowCount(0);
    m_criticalAlertLabel->setText(QStringLiteral("0"));
    m_generalAlarmLabel->setText(QStringLiteral("0"));
    m_signalTotalLabel->setText(QStringLiteral("0"));
    m_frameLabel->setText(QStringLiteral("帧号：-"));
    updateDetectionStatus(nullptr);
}

void MainWindow::startMonitoring()
{
    if (m_monitoring) {
        stopMonitoring();
        return;
    }
    if (!applyCurrentConfiguration()) return;
    clearMonitoringDisplay();
    m_controller.start();
    // start() marks the session active before its worker emits Running.
    updateButtonState(QStringLiteral("Running"));
}

void MainWindow::pauseMonitoring()
{
    if (!m_monitoring) return;
    if (m_stateLabel->text().contains(QStringLiteral("暂停")) ||
        m_stateLabel->text().contains(QStringLiteral("Paused"))) {
        m_controller.resume();
    } else {
        m_controller.pause();
    }
}

void MainWindow::stopMonitoring()
{
    m_controller.stop();
    m_minimumGeneration = m_latestGeneration + 1;
    m_pendingSnapshot.reset();
    updateButtonState(QStringLiteral("Stopped"));
}

void MainWindow::selectMainPage(int index)
{
    m_pages->setCurrentIndex(index);
    m_monitorTab->setChecked(index == 0);
    m_playbackTab->setChecked(index == 1);
    m_settingsTab->setChecked(index == 2);
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, QStringLiteral("关于智能频谱监测仪"),
        QStringLiteral("智能频谱监测仪\n\n"
                       "Qt 6.11.1 / VS 2026\n"
                       "SCN-Based PSD Detection 重构版\n\n"
                       "界面保留原 ISA 的采集监测、录制回放和系统设置工作流。"));
}

void MainWindow::onRecordingStatus(const QString& path, double startFrequencyHz,
                                   double endFrequencyHz, double resolutionBandwidthHz,
                                   std::uint64_t frameCount, bool active)
{
    const QString status = active
        ? QStringLiteral("录制中：%1（%2帧）")
            .arg(QFileInfo(path).fileName())
            .arg(static_cast<qulonglong>(frameCount))
        : QStringLiteral("已保存：%1（%2帧）")
            .arg(QFileInfo(path).fileName())
            .arg(static_cast<qulonglong>(frameCount));
    if (m_settingsPage) m_settingsPage->setRecordingStatus(status);
    statusBar()->showMessage(status, active ? 2500 : 6000);
    if (!path.isEmpty()) {
        const auto sourceName = m_sourceCombo ? m_sourceCombo->currentText()
                                               : QStringLiteral("LIVE");
        std::int64_t startHz = 0, endHz = 0, rbwHz = 0;
        if (scn::common::toIntegerHz(startFrequencyHz, startHz) &&
            scn::common::toIntegerHz(endFrequencyHz, endHz) &&
            scn::common::toIntegerHz(resolutionBandwidthHz, rbwHz)) {
            m_playbackPage->rememberFile(path, sourceName, startHz, endHz, rbwHz, 0, 0);
        }
    }
    if (!active && !path.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("实时频谱录制完成：%1（%2帧）")
            .arg(path).arg(static_cast<qulonglong>(frameCount)), 6000);
    }
}

void MainWindow::onRecordingError(const QString& message)
{
    if (m_settingsPage) m_settingsPage->setRecordingStatus(QStringLiteral("录制失败：%1").arg(message));
    statusBar()->showMessage(QStringLiteral("录制失败：%1").arg(message), 8000);
}

void MainWindow::toggleMaximize()
{
    if (isMaximized()) {
        showNormal();
    } else {
        showMaximized();
    }
}

void MainWindow::updateButtonState(const QString& state)
{
    const bool running = state.contains(QStringLiteral("Running")) || state.contains(QStringLiteral("运行"));
    const bool paused = state.contains(QStringLiteral("Paused")) || state.contains(QStringLiteral("暂停"));
    const bool stopped = state.contains(QStringLiteral("Stopped")) || state.contains(QStringLiteral("停止"));
    if (running || paused) m_monitoring = true;
    if (stopped) m_monitoring = false;
    m_startButton->setText(m_monitoring ? QStringLiteral("停止监测") : QStringLiteral("开始监测"));
    m_startButton->setIcon(style()->standardIcon(m_monitoring ? QStyle::SP_MediaStop
                                                               : QStyle::SP_MediaPlay));
    m_startButton->setObjectName(m_monitoring ? QStringLiteral("dangerButton") : QStringLiteral("primaryButton"));
    m_startButton->style()->unpolish(m_startButton);
    m_startButton->style()->polish(m_startButton);
    m_pauseButton->setEnabled(m_monitoring);
    m_pauseButton->setText(paused ? QStringLiteral("继续查看") : QStringLiteral("暂停查看"));
    m_pauseButton->setIcon(style()->standardIcon(paused ? QStyle::SP_MediaPlay
                                                        : QStyle::SP_MediaPause));
    updateSourceControls();
}

void MainWindow::onSnapshot(const algorithm::DisplaySnapshotPtr& snapshot)
{
    // 采集线程可能快于屏幕刷新；只保留最新快照，避免 UI 事件队列积压旧帧。
    if (!snapshot || snapshot->detection.generation < m_minimumGeneration ||
        snapshot->detection.generation < m_latestGeneration) return;
    m_latestGeneration = snapshot->detection.generation;
    // A short file may reach EOF before Running is delivered. Its final result
    // is still valid even when snapshot.running is false or startup is pending.
    m_pendingSnapshot = snapshot;
}

void MainWindow::refreshDisplay()
{
    const auto snapshot = m_pendingSnapshot;
    if (!snapshot && !m_pendingPolicySnapshot) return;

    if (snapshot) {
        m_pendingSnapshot.reset();
        if (m_displaySnapshot &&
            m_displaySnapshot->detection.generation != snapshot->detection.generation) {
            clearMonitoringDisplay(false);
        }
        if (!m_displaySnapshot) m_timeOriginNs = snapshot->frame.timestampNs;
        const bool newRawFrame = !m_displaySnapshot ||
            m_displaySnapshot->frame.sequence != snapshot->frame.sequence;
        if (newRawFrame) {
            if (!m_displayRateTimer.isValid()) m_displayRateTimer.start();
            ++m_displayRateFrames;
        }
        m_displaySnapshot = snapshot;
        const double endFrequencyHz = snapshot->frame.startFrequencyHz +
            snapshot->frame.binWidthHz * static_cast<double>(snapshot->frame.powerDb.size());
        m_spectrum->setSnapshot(snapshot);
        m_waterfall->setSnapshot(snapshot);
        syncFrequencyNavigator();
        m_frameLabel->setText(QStringLiteral("帧号：%1").arg(snapshot->frame.sequence));
        const bool fileSource = static_cast<algorithm::SourceKind>(m_sourceCombo->currentData().toInt()) ==
            algorithm::SourceKind::File;
        if (fileSource) {
            m_statusStrip->setMenuInfo((snapshot->frame.startFrequencyHz + endFrequencyHz) / 2.0,
                                       endFrequencyHz - snapshot->frame.startFrequencyHz,
                                       static_cast<double>(m_resolutionBandwidth->frequencyHz()));
        } else {
            m_statusStrip->setMenuInfoVisible(false);
        }
        updateDetectionStatus(snapshot.get());
    }

    if (!m_displaySnapshot) return;
    const auto& displaySnapshot = *m_displaySnapshot;
    bool policyChanged = false;
    if (m_pendingPolicySnapshot &&
        m_pendingPolicySnapshot->generation == displaySnapshot.detection.generation &&
        m_pendingPolicySnapshot->detectionConfigVersion == displaySnapshot.detection.configVersion &&
        m_pendingPolicySnapshot->sequence <= displaySnapshot.frame.sequence) {
        m_policySnapshot = std::move(m_pendingPolicySnapshot);
        m_spectrum->setPolicySnapshot(m_policySnapshot);
        policyChanged = true;
    }

    const auto& snapshotRef = displaySnapshot;
    const bool fileSource = static_cast<algorithm::SourceKind>(m_sourceCombo->currentData().toInt()) ==
        algorithm::SourceKind::File;
    if (snapshot) {
        const auto& current = *snapshot;
        const auto& data = current.detection;
        const DetectionKey key{data.generation, data.configVersion, data.sequence, data.stage};
        if (!m_lastDetectionKey || *m_lastDetectionKey != key) {
            m_lastDetectionKey = key;
            updateMonitorMetrics(current);
            updateSignalTable(current);
            if (fileSource) {
                const double endFrequencyHz = current.frame.startFrequencyHz +
                    current.frame.binWidthHz * static_cast<double>(current.frame.powerDb.size());
                std::int64_t startHz = 0, endHz = 0;
                if (scn::common::toIntegerHz(current.frame.startFrequencyHz, startHz) &&
                    scn::common::toIntegerHz(endFrequencyHz, endHz)) {
                    m_playbackPage->rememberFile(m_filePath->text(), m_sourceCombo->currentText(),
                        startHz, endHz, m_resolutionBandwidth->frequencyHz(),
                        hasDetectionObservations(data) ? static_cast<int>(data.detections.size()) : 0, 0);
                }
                updatePlaybackResultSignals(current);
            }
        }
    }
    if (policyChanged) {
        updateMonitorMetrics(snapshotRef);
        updateSignalTable(snapshotRef);
        if (fileSource) updatePlaybackResultSignals(snapshotRef);
    }
}

void MainWindow::updateMonitorMetrics(const algorithm::DisplaySnapshot& snapshot)
{
    const bool policyUsable = m_policySnapshot &&
        m_policySnapshot->generation == snapshot.detection.generation &&
        m_policySnapshot->detectionConfigVersion == snapshot.detection.configVersion &&
        m_policySnapshot->sequence <= snapshot.frame.sequence;
    if (!policyUsable) return;
    m_criticalAlertLabel->setText(QString::number(static_cast<qulonglong>(m_policySnapshot->activeCriticalCount)));
    m_generalAlarmLabel->setText(QString::number(static_cast<qulonglong>(m_policySnapshot->activeGeneralCount)));
    const auto count = m_policySnapshot->businessSignals.size();
    m_signalTotalLabel->setText(QString::number(static_cast<qulonglong>(count)));
}

void MainWindow::updateDetectionStatus(const algorithm::DisplaySnapshot* snapshot)
{
    if (!m_detectionStatusLabel) return;
    const auto config = m_appliedDetectionConfig.value_or(algorithm::DetectionConfig{});
    QString state = config.enabled ? QStringLiteral("等待模型／数据") : QStringLiteral("已关闭");
    QString modelInfo;
    QString message;
    algorithm::DetectionDiagnostics diagnostics;
    std::size_t accumulated = 0;
    std::size_t required = config.accumulator.frames;
    QString sequenceAge = QStringLiteral("—");
    std::uint64_t dropped = 0;
    if (snapshot) {
        const auto& data = snapshot->detection;
        diagnostics = data.diagnostics;
        accumulated = data.accumulatedFrames;
        required = data.requiredFrames;
        modelInfo = QString::fromStdString(diagnostics.modelInfo);
        message = QString::fromStdString(diagnostics.message);
        dropped = snapshot->droppedFrames;
        switch (data.stage) {
        case algorithm::DetectionStage::Accumulating: state = QStringLiteral("部分累计"); break;
        case algorithm::DetectionStage::Completed: state = QStringLiteral("已完成"); break;
        case algorithm::DetectionStage::Error: state = QStringLiteral("检测失败"); break;
        case algorithm::DetectionStage::Cancelled: state = QStringLiteral("已取消"); break;
        case algorithm::DetectionStage::Bypassed:
            state = config.enabled ? QStringLiteral("等待检测") : QStringLiteral("已关闭");
            break;
        }
        if (data.pointCount > 0 && snapshot->frame.sequence >= data.sequence) {
            sequenceAge = QString::number(snapshot->frame.sequence - data.sequence);
        }
    }
    if (m_detectionModelStatus.contains(QStringLiteral("unavailable"), Qt::CaseInsensitive)) {
        state = QStringLiteral("模型不可用");
    }
    m_detectionStatusLabel->setText(QStringLiteral(
        "SCN：%1 | 累计 %2/%3 | 检测 %4 ms | 延迟 %5 ms | 丢帧 %6")
        .arg(state).arg(static_cast<qulonglong>(accumulated)).arg(static_cast<qulonglong>(required))
        .arg(diagnostics.processingTimeMs, 0, 'f', 1).arg(diagnostics.resultLatencyMs, 0, 'f', 1)
        .arg(dropped));
    QStringList details{
        QStringLiteral("模型状态：%1").arg(m_detectionModelStatus),
        QStringLiteral("模型路径：%1").arg(QString::fromStdString(config.detector.modelPath)),
        QStringLiteral("GPU：%1").arg(config.detector.deviceIndex),
        QStringLiteral("模型信息：%1").arg(modelInfo.isEmpty() ? QStringLiteral("—") : modelInfo),
        QStringLiteral("排队：%1 | 已完成：%2")
            .arg(static_cast<qulonglong>(diagnostics.queueDepth)).arg(diagnostics.completedCount),
        QStringLiteral("检测 P50 / P95：%1 / %2 ms")
            .arg(diagnostics.processingP50Ms, 0, 'f', 1).arg(diagnostics.processingP95Ms, 0, 'f', 1),
        QStringLiteral("检测完成吞吐：%1 帧/s | UI原始帧提交：%2 帧/s（非GPU呈现FPS）")
            .arg(diagnostics.throughputHz, 0, 'f', 2).arg(m_displayRateHz, 0, 'f', 2),
        QStringLiteral("结果落后原始帧：%1 帧").arg(sequenceAge),
        QStringLiteral("累计 / 推理 / 后处理：%1 / %2 / %3 ms")
            .arg(diagnostics.accumulationTimeMs, 0, 'f', 1)
            .arg(diagnostics.inferenceTimeMs, 0, 'f', 1)
            .arg(diagnostics.postprocessTimeMs, 0, 'f', 1),
        QStringLiteral("窗口 / 候选 / CNR 通过 / 截断：%1 / %2 / %3 / %4")
            .arg(static_cast<qulonglong>(diagnostics.windowCount))
            .arg(static_cast<qulonglong>(diagnostics.candidateCount))
            .arg(static_cast<qulonglong>(diagnostics.cnrAcceptedCount))
            .arg(static_cast<qulonglong>(diagnostics.truncatedCount))};
    if (snapshot) {
        const auto& data = snapshot->detection;
        details << QStringLiteral("轮次 / 配置版本 / 结果序号：%1 / %2 / %3")
            .arg(data.generation).arg(data.configVersion).arg(data.sequence);
        details << QStringLiteral("累计起始序号：%1 | 原始帧序号：%2")
            .arg(data.firstSequence).arg(snapshot->frame.sequence);
    }
    if (!message.isEmpty()) details << message;
    m_detectionStatusLabel->setToolTip(details.join(QLatin1Char('\n')));
}

QString MainWindow::formatDetectionTime(std::int64_t timestampNs, bool fileSource) const
{
    if (fileSource) {
        return QStringLiteral("回放 %1 s").arg(static_cast<double>(timestampNs) / 1.0e9, 0, 'f', 3);
    }
    // Hardware timestamps are monotonic, not Unix epoch. Keep the origin fixed
    // for this generation; earlier observations may precede the first UI frame.
    const double seconds = static_cast<double>(static_cast<long double>(timestampNs) -
                                               static_cast<long double>(m_timeOriginNs)) / 1.0e9;
    return QStringLiteral("本轮首帧%1%2 s")
        .arg(seconds >= 0.0 ? QStringLiteral("+") : QString())
        .arg(seconds, 0, 'f', 3);
}

QString MainWindow::signalDetails(const policy::PolicySignal& businessSignal, bool fileSource) const
{
    const auto& signal = businessSignal.measurement;
    QString branch;
    switch (businessSignal.measurementBranch) {
    case algorithm::SpectrumBranch::Average: branch = QStringLiteral("平均谱（Average）"); break;
    case algorithm::SpectrumBranch::Maximum: branch = QStringLiteral("最大谱（Maximum）"); break;
    case algorithm::SpectrumBranch::Both: branch = QStringLiteral("双分支融合（Both）"); break;
    }
    QStringList details{
        QStringLiteral("信号 ID：%1").arg(QString::fromStdString(businessSignal.displayId)),
        QStringLiteral("结果来源：%1").arg(businessSignal.source == policy::PolicySignalSource::Whitelist
            ? QStringLiteral("白名单替换") : QStringLiteral("SCN 原始检测")),
        QStringLiteral("起始频率：%1").arg(formatFrequency(signal.startFrequencyHz)),
        QStringLiteral("终止频率：%1").arg(formatFrequency(signal.endFrequencyHz)),
        QStringLiteral("中心频率：%1").arg(formatFrequency(signal.centerFrequencyHz)),
        QStringLiteral("带宽：%1").arg(formatFrequency(signal.bandwidthHz)),
        QStringLiteral("置信度：%1").arg(signal.confidence, 0, 'f', 6),
        QStringLiteral("CNR（snrDb）：%1 dB").arg(signal.snrDb, 0, 'f', 3),
        QStringLiteral("信号电平：%1 dBm").arg(signal.signalLevelDbm, 0, 'f', 3),
        QStringLiteral("噪声电平：%1 dBm").arg(signal.noiseLevelDbm, 0, 'f', 3),
        QStringLiteral("检测分支：%1").arg(branch),
        QStringLiteral("首次出现：%1").arg(formatDetectionTime(signal.firstSeenNs, fileSource)),
        QStringLiteral("最近出现：%1").arg(formatDetectionTime(signal.lastSeenNs, fileSource)),
        QStringLiteral("firstSeenNs：%1 | lastSeenNs：%2").arg(signal.firstSeenNs).arg(signal.lastSeenNs),
        QStringLiteral("出现次数：%1").arg(signal.occurrenceCount),
        QStringLiteral("信号类型：未分类"),
        fileSource ? QStringLiteral("回放时间由文件帧位置与帧率生成，与实际播放速度无关。")
                   : QStringLiteral("硬件时间相对本轮首个显示帧；负值表示更早的观测，不是日历时间。")};
    if (businessSignal.hasBoundaryMetadata) {
        const auto& stable = businessSignal.stableMeasurement;
        details << QStringLiteral("原始检测频段：%1 ~ %2")
            .arg(formatFrequency(businessSignal.rawMeasurement.startFrequencyHz),
                 formatFrequency(businessSignal.rawMeasurement.endFrequencyHz));
        details << QStringLiteral("稳定业务频段：%1 ~ %2")
            .arg(formatFrequency(stable.startFrequencyHz), formatFrequency(stable.endFrequencyHz));
        details << QStringLiteral("稳定重测：信号 %1 dBm，噪声 %2 dBm，CNR %3 dB；测量分支：%4")
            .arg(stable.signalLevelDbm, 0, 'f', 3)
            .arg(stable.noiseLevelDbm, 0, 'f', 3)
            .arg(stable.snrDb, 0, 'f', 3)
            .arg(branch);
        QString boundaryState;
        switch (businessSignal.boundaryState) {
        case algorithm::BoundaryState::Stable: boundaryState = QStringLiteral("稳定"); break;
        case algorithm::BoundaryState::PendingChange:
            boundaryState = QStringLiteral("边界变化待确认 %1/%2")
                .arg(businessSignal.pendingBoundaryCount).arg(businessSignal.requiredBoundaryCount); break;
        case algorithm::BoundaryState::Ambiguous: boundaryState = QStringLiteral("关联歧义，新建身份"); break;
        case algorithm::BoundaryState::Disabled: boundaryState = QStringLiteral("稳定功能关闭"); break;
        }
        details << QStringLiteral("边界状态：%1").arg(boundaryState);
        details << QStringLiteral("关联诊断：IoU %1，中心距离 %2，带宽比 %3；%4")
            .arg(businessSignal.associationIou, 0, 'f', 4)
            .arg(formatFrequency(businessSignal.associationCenterDistanceHz))
            .arg(businessSignal.associationBandwidthRatio, 0, 'f', 3)
            .arg(QString::fromStdString(businessSignal.boundaryDiagnostic));
    }
    if (businessSignal.source == policy::PolicySignalSource::Whitelist) {
        details << QStringLiteral("白名单频段：%1 ~ %2")
            .arg(formatFrequency(signal.startFrequencyHz), formatFrequency(signal.endFrequencyHz));
        details << QStringLiteral("代表原始信号 ID：%1")
            .arg(businessSignal.representativeSignalId);
        QStringList originalIds;
        for (const auto id : businessSignal.originalSignalIds) originalIds << QString::number(id);
        details << QStringLiteral("归并原始信号：%1")
            .arg(originalIds.isEmpty() ? QStringLiteral("无") : originalIds.join(QStringLiteral("、")));
    }
    if (const auto* annotation = annotationFor(businessSignal.source, businessSignal.id)) {
        QStringList names;
        for (const auto& name : annotation->whitelistNames) names << QString::fromStdString(name);
        details << QStringLiteral("白名单：%1").arg(names.isEmpty() ? QStringLiteral("无") : names.join(QStringLiteral("、")));
        const auto level = annotation->level == policy::AlarmLevel::Critical ? QStringLiteral("严重")
            : annotation->level == policy::AlarmLevel::General ? QStringLiteral("一般") : QStringLiteral("无");
        const auto state = annotation->state == policy::AlarmState::Pending ? QStringLiteral("待确认")
            : annotation->state == policy::AlarmState::PendingClear ? QStringLiteral("待解除")
            : annotation->state == policy::AlarmState::Active ? QStringLiteral("活动") : QStringLiteral("无");
        details << QStringLiteral("告警等级：%1 | 状态：%2").arg(level, state);
        QStringList matched;
        for (const auto& rule : annotation->ruleMatches)
            if (rule.matched) matched << QString::fromStdString(rule.ruleName);
        details << QStringLiteral("命中规则：%1").arg(matched.isEmpty() ? QStringLiteral("无") : matched.join(QStringLiteral("、")));
    }
    return details.join(QLatin1Char('\n'));
}

const policy::SignalAnnotation* MainWindow::annotationFor(policy::PolicySignalSource source,
                                                          std::int64_t signalId) const
{
    if (!m_policySnapshot) return nullptr;
    const auto it = std::find_if(m_policySnapshot->annotations.begin(), m_policySnapshot->annotations.end(),
        [source, signalId](const policy::SignalAnnotation& annotation) {
            return annotation.source == source && annotation.signalId == signalId;
        });
    return it == m_policySnapshot->annotations.end() ? nullptr : &*it;
}

void MainWindow::updateSignalTable(const algorithm::DisplaySnapshot& snapshot)
{
    const bool fileSource = m_sourceCombo->currentData().toInt() == static_cast<int>(algorithm::SourceKind::File);
    const bool policyUsable = m_policySnapshot &&
        m_policySnapshot->generation == snapshot.detection.generation &&
        m_policySnapshot->detectionConfigVersion == snapshot.detection.configVersion &&
        m_policySnapshot->sequence <= snapshot.frame.sequence;
    if (!policyUsable) return;
    const int count = static_cast<int>(m_policySnapshot->businessSignals.size());
    m_signalTable->setRowCount(count);
    for (int row = 0; row < count; ++row) {
        const auto& businessSignal = m_policySnapshot->businessSignals.at(static_cast<std::size_t>(row));
        const auto& signal = businessSignal.measurement;
        auto* id = tableItem(QString::fromStdString(businessSignal.displayId));
        const auto details = signalDetails(businessSignal, fileSource);
        id->setData(Qt::UserRole, details);
        id->setToolTip(details);
        m_signalTable->setItem(row, 0, id);
        m_signalTable->setItem(row, 1, tableItem(formatFrequency(signal.centerFrequencyHz)));
        m_signalTable->setItem(row, 2, tableItem(formatFrequency(signal.bandwidthHz)));
        m_signalTable->setItem(row, 3, tableItem(QStringLiteral("未分类")));
        QString alarmLevel = QStringLiteral("无");
        if (const auto* annotation = annotationFor(businessSignal.source, businessSignal.id)) {
            if (annotation->level == policy::AlarmLevel::Critical) alarmLevel = QStringLiteral("严重");
            else if (annotation->level == policy::AlarmLevel::General) alarmLevel = QStringLiteral("一般");
            if (annotation->state == policy::AlarmState::Pending) alarmLevel += QStringLiteral("（待确认）");
            else if (annotation->state == policy::AlarmState::PendingClear) alarmLevel += QStringLiteral("（待解除）");
            if (!annotation->whitelistNames.empty()) alarmLevel += QStringLiteral(" | 白名单");
        }
        m_signalTable->setItem(row, 4, tableItem(alarmLevel));
        auto* lastSeen = tableItem(formatDetectionTime(signal.lastSeenNs, fileSource));
        lastSeen->setToolTip(details);
        m_signalTable->setItem(row, 5, lastSeen);
        m_signalTable->setItem(row, 6, tableItem(QString::number(signal.occurrenceCount)));
    }
}

void MainWindow::updatePlaybackResultSignals(const algorithm::DisplaySnapshot& snapshot)
{
    if (!m_playbackPage || !m_filePath || m_filePath->text().trimmed().isEmpty()) return;
    if (!m_policySnapshot ||
        m_policySnapshot->generation != snapshot.detection.generation ||
        m_policySnapshot->detectionConfigVersion != snapshot.detection.configVersion ||
        m_policySnapshot->sequence > snapshot.frame.sequence) return;

    QVector<PlaybackSignalRow> rows;
    rows.reserve(static_cast<int>(m_policySnapshot->businessSignals.size()));
    for (const auto& businessSignal : m_policySnapshot->businessSignals) {
        const auto& signal = businessSignal.measurement;
        QString alarm = QStringLiteral("无");
        if (const auto* annotation = annotationFor(businessSignal.source, businessSignal.id)) {
            if (annotation->level == policy::AlarmLevel::Critical) alarm = QStringLiteral("严重");
            else if (annotation->level == policy::AlarmLevel::General) alarm = QStringLiteral("一般");
            if (annotation->state == policy::AlarmState::Pending) alarm += QStringLiteral("（待确认）");
            else if (annotation->state == policy::AlarmState::PendingClear) alarm += QStringLiteral("（待解除）");
            if (!annotation->whitelistNames.empty()) alarm += QStringLiteral(" | 白名单");
        }
        PlaybackSignalRow row;
        row.id = QString::fromStdString(businessSignal.displayId);
        row.centerFrequencyHz = signal.centerFrequencyHz;
        row.bandwidthHz = signal.bandwidthHz;
        row.type = QStringLiteral("未分类");
        row.alarm = alarm;
        row.lastSeen = formatDetectionTime(signal.lastSeenNs, true);
        row.occurrenceCount = QString::number(signal.occurrenceCount);
        row.details = signalDetails(businessSignal, true);
        rows.push_back(std::move(row));
    }
    m_playbackPage->updateResultSignals(m_filePath->text().trimmed(), rows);
}

void MainWindow::applyPolicyConfiguration()
{
    if (!m_settingsPage) return;
    QString error;
    auto config = m_settingsPage->policyConfig(&error);
    if (!error.isEmpty()) {
        m_settingsPage->setDetectionFeedback(QStringLiteral("白名单与告警规则未应用：%1").arg(error));
        return;
    }
    const auto previous = m_session.policyConfig();
    config.version = std::max(config.version, previous.version + 1);
    if (!m_settingsPage->acceptPolicyConfig(config, &error)) {
        m_settingsPage->setDetectionFeedback(QStringLiteral("白名单与告警规则未应用：无法保存配置：%1").arg(error));
        return;
    }
    if (!m_session.configurePolicy(config, error)) {
        QString rollbackError;
        m_settingsPage->acceptPolicyConfig(previous, &rollbackError);
        m_settingsPage->setDetectionFeedback(QStringLiteral("白名单与告警规则未应用：%1").arg(error));
        return;
    }
    m_policySnapshot.reset();
    m_pendingPolicySnapshot.reset();
    m_spectrum->setPolicySnapshot(nullptr);
    m_signalTable->setRowCount(0);
    m_criticalAlertLabel->setText(QStringLiteral("0"));
    m_generalAlarmLabel->setText(QStringLiteral("0"));
    m_signalTotalLabel->setText(QStringLiteral("0"));
    if (m_displaySnapshot) {
        updateDetectionStatus(m_displaySnapshot.get());
    }
}

void MainWindow::onPolicySnapshot(const policy::PolicySnapshotPtr& snapshot)
{
    if (!snapshot || snapshot->generation < m_minimumGeneration) return;
    const policy::PolicySnapshot* latest = nullptr;
    if (m_pendingPolicySnapshot) latest = m_pendingPolicySnapshot.get();
    else if (m_policySnapshot) latest = m_policySnapshot.get();
    if (latest && snapshot->generation == latest->generation &&
        (snapshot->sequence < latest->sequence ||
         (snapshot->sequence == latest->sequence &&
          snapshot->revision < latest->revision))) return;
    // Policy callbacks can arrive between two raw-frame publications. Keep
    // only the newest result and commit it from refreshDisplay(), so markers,
    // table and statistics share one UI cadence and cannot flash independently.
    m_pendingPolicySnapshot = snapshot;
}

void MainWindow::onAlarmEvents(const std::vector<policy::AlarmEventChange>& changes)
{
    if (changes.empty()) return;
    m_policyEventRevision = std::max(m_policyEventRevision, changes.back().revision);
    statusBar()->showMessage(QStringLiteral("告警事件已更新（本地历史已排队保存）"), 2500);
}

void MainWindow::onPolicyStatus(const QString& message)
{
    statusBar()->showMessage(message, 4000);
}

void MainWindow::showAlarmHistory()
{
    std::vector<policy::AlarmEvent> events;
    QString error;
    if (!m_session.loadAlarmHistory(events, error)) {
        QMessageBox::warning(this, QStringLiteral("告警历史不可用"), error);
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("告警历史"));
    dialog.resize(1180, 560);
    auto* layout = new QVBoxLayout(&dialog);
    auto* filters = new QHBoxLayout;
    auto* levelFilter = new QComboBox(&dialog);
    levelFilter->addItems({QStringLiteral("全部等级"), QStringLiteral("一般"), QStringLiteral("严重")});
    auto* acknowledgementFilter = new QComboBox(&dialog);
    acknowledgementFilter->addItems({QStringLiteral("全部确认状态"), QStringLiteral("未确认"), QStringLiteral("已确认")});
    auto* sourceFilter = new QLineEdit(&dialog);
    sourceFilter->setPlaceholderText(QStringLiteral("源名称"));
    auto* ruleFilter = new QLineEdit(&dialog);
    ruleFilter->setPlaceholderText(QStringLiteral("规则 ID"));
    auto* whitelistFilter = new QLineEdit(&dialog);
    whitelistFilter->setPlaceholderText(QStringLiteral("白名单 ID"));
    auto* activeOnly = new QCheckBox(QStringLiteral("仅活动"), &dialog);
    filters->addWidget(new QLabel(QStringLiteral("筛选"), &dialog));
    filters->addWidget(levelFilter); filters->addWidget(acknowledgementFilter);
    filters->addWidget(sourceFilter); filters->addWidget(ruleFilter);
    filters->addWidget(whitelistFilter); filters->addWidget(activeOnly);
    filters->addStretch();
    layout->addLayout(filters);
    auto* table = new QTableWidget(static_cast<int>(events.size()), 12, &dialog);
    table->setObjectName(QStringLiteral("isaTable"));
    table->setHorizontalHeaderLabels({QStringLiteral("事件 ID"), QStringLiteral("业务 ID"), QStringLiteral("来源"),
        QStringLiteral("频段"), QStringLiteral("带宽"), QStringLiteral("当前/最高等级"),
        QStringLiteral("CNR(dB)"), QStringLiteral("边界状态"), QStringLiteral("状态"), QStringLiteral("确认"),
        QStringLiteral("源"), QStringLiteral("结束原因")});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    auto levelText = [](policy::AlarmLevel level) {
        return level == policy::AlarmLevel::Critical ? QStringLiteral("严重")
            : level == policy::AlarmLevel::General ? QStringLiteral("一般") : QStringLiteral("无");
    };
    auto stateText = [](policy::AlarmState state) {
        return state == policy::AlarmState::Active ? QStringLiteral("活动")
            : state == policy::AlarmState::Pending ? QStringLiteral("待确认")
            : state == policy::AlarmState::PendingClear ? QStringLiteral("待解除")
            : QStringLiteral("已解除");
    };
    for (int row = 0; row < table->rowCount(); ++row) {
        const auto& event = events.at(static_cast<std::size_t>(row));
        auto* id = tableItem(QString::fromStdString(event.eventId));
        id->setData(Qt::UserRole, QString::fromStdString(event.eventId));
        table->setItem(row, 0, id);
        const auto businessId = event.displayId.empty()
            ? QString::number(event.signalId) : QString::fromStdString(event.displayId);
        table->setItem(row, 1, tableItem(businessId));
        table->setItem(row, 2, tableItem(event.source == policy::PolicySignalSource::Whitelist
            ? QStringLiteral("白名单替换") : QStringLiteral("原始检测")));
        table->setItem(row, 3, tableItem(QStringLiteral("%1 ~ %2")
            .arg(formatFrequency(event.startFrequencyHz), formatFrequency(event.endFrequencyHz))));
        table->setItem(row, 4, tableItem(formatFrequency(event.bandwidthHz)));
        table->setItem(row, 5, tableItem(levelText(event.currentLevel) + QStringLiteral(" / ") + levelText(event.highestLevel)));
        table->setItem(row, 6, tableItem(QString::number(event.cnrDb, 'f', 2)));
        QString boundaryText;
        if (!event.hasBoundaryMetadata) boundaryText = QStringLiteral("旧版/未记录");
        else switch (event.boundaryState) {
        case algorithm::BoundaryState::Stable: boundaryText = QStringLiteral("稳定"); break;
        case algorithm::BoundaryState::PendingChange:
            boundaryText = QStringLiteral("待确认 %1/%2").arg(event.pendingBoundaryCount).arg(event.requiredBoundaryCount); break;
        case algorithm::BoundaryState::Ambiguous: boundaryText = QStringLiteral("关联歧义"); break;
        case algorithm::BoundaryState::Disabled: boundaryText = QStringLiteral("稳定功能关闭"); break;
        }
        auto* boundaryItem = tableItem(boundaryText);
        boundaryItem->setToolTip(event.hasBoundaryMetadata
            ? QStringLiteral("原始：%1 ~ %2\n稳定：%3 ~ %4\n测量分支：%5")
                .arg(formatFrequency(event.rawStartFrequencyHz), formatFrequency(event.rawEndFrequencyHz),
                     formatFrequency(event.stableStartFrequencyHz), formatFrequency(event.stableEndFrequencyHz))
                .arg(static_cast<int>(event.measurementBranch))
            : QStringLiteral("旧版事件没有保存原始/稳定边界信息。"));
        table->setItem(row, 7, boundaryItem);
        table->setItem(row, 8, tableItem(stateText(event.state)));
        table->setItem(row, 9, tableItem(event.acknowledged ? QStringLiteral("已确认") : QStringLiteral("未确认")));
        table->setItem(row, 10, tableItem(QString::fromStdString(event.sourceName)));
        table->setItem(row, 11, tableItem(QString::fromStdString(event.endReason)));
    }
    const auto applyFilters = [&, table, levelFilter, acknowledgementFilter, sourceFilter,
                               ruleFilter, whitelistFilter, activeOnly] {
        const auto sourceText = sourceFilter->text().trimmed();
        const auto ruleText = ruleFilter->text().trimmed();
        const auto whitelistText = whitelistFilter->text().trimmed();
        for (int row = 0; row < table->rowCount(); ++row) {
            const auto& event = events.at(static_cast<std::size_t>(row));
            bool visible = levelFilter->currentIndex() == 0 ||
                (levelFilter->currentIndex() == 1 && event.currentLevel == policy::AlarmLevel::General) ||
                (levelFilter->currentIndex() == 2 && event.currentLevel == policy::AlarmLevel::Critical);
            visible = visible && (acknowledgementFilter->currentIndex() == 0 ||
                (acknowledgementFilter->currentIndex() == 1 && !event.acknowledged) ||
                (acknowledgementFilter->currentIndex() == 2 && event.acknowledged));
            visible = visible && (sourceText.isEmpty() || QString::fromStdString(event.sourceName).contains(sourceText, Qt::CaseInsensitive));
            if (visible && !ruleText.isEmpty()) {
                bool found = false;
                for (const auto id : event.matchedRuleIds) if (QString::number(id) == ruleText) found = true;
                visible = found;
            }
            if (visible && !whitelistText.isEmpty()) {
                bool found = false;
                for (const auto id : event.matchedWhitelistIds) if (QString::number(id) == whitelistText) found = true;
                visible = found;
            }
            if (activeOnly->isChecked())
                visible = visible && (event.state == policy::AlarmState::Active || event.state == policy::AlarmState::PendingClear);
            table->setRowHidden(row, !visible);
        }
    };
    connect(levelFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog, [applyFilters] { applyFilters(); });
    connect(acknowledgementFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog, [applyFilters] { applyFilters(); });
    connect(sourceFilter, &QLineEdit::textChanged, &dialog, [applyFilters] { applyFilters(); });
    connect(ruleFilter, &QLineEdit::textChanged, &dialog, [applyFilters] { applyFilters(); });
    connect(whitelistFilter, &QLineEdit::textChanged, &dialog, [applyFilters] { applyFilters(); });
    connect(activeOnly, &QCheckBox::toggled, &dialog, [applyFilters] { applyFilters(); });
    layout->addWidget(table, 1);
    auto* buttons = new QHBoxLayout;
    const auto makeAction = [&dialog](const QString& text) {
        auto* button = new QPushButton(text, &dialog);
        button->setMinimumHeight(34);
        return button;
    };
    auto* acknowledge = makeAction(QStringLiteral("确认选中事件"));
    auto* exportCsv = makeAction(QStringLiteral("导出 CSV"));
    auto* exportJson = makeAction(QStringLiteral("导出 JSON"));
    buttons->addWidget(acknowledge); buttons->addWidget(exportCsv); buttons->addWidget(exportJson);
    buttons->addStretch();
    auto* close = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    buttons->addWidget(close);
    layout->addLayout(buttons);
    connect(close, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(acknowledge, &QPushButton::clicked, &dialog, [this, table] {
        const int row = table->currentRow();
        if (row < 0 || !table->item(row, 0)) return;
        bool ok = false;
        const auto note = QInputDialog::getText(this, QStringLiteral("确认告警事件"),
            QStringLiteral("确认备注（可选）："), QLineEdit::Normal, QString(), &ok);
        if (ok && m_session.acknowledgeAlarm(table->item(row, 0)->data(Qt::UserRole).toString(), note))
            statusBar()->showMessage(QStringLiteral("确认请求已提交。请重新打开历史查看最新状态。"), 4000);
    });
    auto csvEscape = [](const QString& value) {
        QString result = value; result.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        return QStringLiteral("\"") + result + QStringLiteral("\"");
    };
    connect(exportCsv, &QPushButton::clicked, &dialog, [this, &events, csvEscape] {
        const auto path = QFileDialog::getSaveFileName(this, QStringLiteral("导出告警 CSV"),
            QStringLiteral("alarm_history.csv"), QStringLiteral("CSV files (*.csv)"));
        if (path.isEmpty()) return;
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;
        QTextStream out(&file);
        out << QStringLiteral("event_id,business_id,source_type,signal_id,representative_signal_id,original_signal_ids,start_hz,end_hz,bandwidth_hz,has_boundary_metadata,raw_start_hz,raw_end_hz,stable_start_hz,stable_end_hz,boundary_state,boundary_pending_count,boundary_required_count,measurement_branch,current_level,highest_level,state,acknowledged,policy_version,source,end_reason\n");
        for (const auto& event : events) {
            out << csvEscape(QString::fromStdString(event.eventId)) << ','
                << csvEscape(event.displayId.empty() ? QString::number(event.signalId) : QString::fromStdString(event.displayId)) << ','
                << static_cast<int>(event.source) << ',' << event.signalId << ',' << event.representativeSignalId << ','
                << csvEscape([&event] { QStringList values; for (const auto id : event.originalSignalIds) values << QString::number(id); return values.join(QLatin1Char(',')); }()) << ','
                << event.startFrequencyHz << ',' << event.endFrequencyHz << ',' << event.bandwidthHz << ','
                << (event.hasBoundaryMetadata ? 1 : 0) << ','
                << event.rawStartFrequencyHz << ',' << event.rawEndFrequencyHz << ','
                << event.stableStartFrequencyHz << ',' << event.stableEndFrequencyHz << ','
                << static_cast<int>(event.boundaryState) << ',' << event.pendingBoundaryCount << ','
                << event.requiredBoundaryCount << ',' << static_cast<int>(event.measurementBranch) << ','
                << static_cast<int>(event.currentLevel) << ',' << static_cast<int>(event.highestLevel) << ','
                << static_cast<int>(event.state) << ',' << (event.acknowledged ? 1 : 0) << ','
                << event.policyVersion << ','
                << csvEscape(QString::fromStdString(event.sourceName)) << ','
                << csvEscape(QString::fromStdString(event.endReason)) << '\n';
        }
        statusBar()->showMessage(QStringLiteral("告警历史已导出：%1").arg(path), 4000);
    });
    connect(exportJson, &QPushButton::clicked, &dialog, [this, &events] {
        const auto path = QFileDialog::getSaveFileName(this, QStringLiteral("导出告警 JSON"),
            QStringLiteral("alarm_history.json"), QStringLiteral("JSON files (*.json)"));
        if (path.isEmpty()) return;
        QJsonArray array;
        for (const auto& event : events) {
            QJsonObject object{{QStringLiteral("eventId"), QString::fromStdString(event.eventId)},
                {QStringLiteral("businessId"), event.displayId.empty() ? QString::number(event.signalId) : QString::fromStdString(event.displayId)},
                {QStringLiteral("sourceType"), static_cast<int>(event.source)}, {QStringLiteral("signalId"), event.signalId},
                {QStringLiteral("representativeSignalId"), event.representativeSignalId},
                {QStringLiteral("originalSignalIds"), [&event] {
                    QJsonArray ids; for (const auto id : event.originalSignalIds) ids.append(id); return ids;
                }()},
                {QStringLiteral("startFrequencyHz"), event.startFrequencyHz},
                {QStringLiteral("endFrequencyHz"), event.endFrequencyHz}, {QStringLiteral("bandwidthHz"), event.bandwidthHz},
                {QStringLiteral("rawStartFrequencyHz"), event.rawStartFrequencyHz},
                {QStringLiteral("rawEndFrequencyHz"), event.rawEndFrequencyHz},
                {QStringLiteral("stableStartFrequencyHz"), event.stableStartFrequencyHz},
                {QStringLiteral("stableEndFrequencyHz"), event.stableEndFrequencyHz},
                {QStringLiteral("boundaryState"), static_cast<int>(event.boundaryState)},
                {QStringLiteral("hasBoundaryMetadata"), event.hasBoundaryMetadata},
                {QStringLiteral("boundaryPendingCount"), static_cast<qint64>(event.pendingBoundaryCount)},
                {QStringLiteral("boundaryRequiredCount"), static_cast<qint64>(event.requiredBoundaryCount)},
                {QStringLiteral("measurementBranch"), static_cast<int>(event.measurementBranch)},
                {QStringLiteral("currentLevel"), static_cast<int>(event.currentLevel)},
                {QStringLiteral("highestLevel"), static_cast<int>(event.highestLevel)},
                {QStringLiteral("state"), static_cast<int>(event.state)}, {QStringLiteral("acknowledged"), event.acknowledged},
                {QStringLiteral("policyVersion"), static_cast<qint64>(event.policyVersion)},
                {QStringLiteral("source"), QString::fromStdString(event.sourceName)},
                {QStringLiteral("endReason"), QString::fromStdString(event.endReason)}};
            array.append(object);
        }
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;
        file.write(QJsonDocument(array).toJson(QJsonDocument::Indented));
        statusBar()->showMessage(QStringLiteral("告警历史已导出：%1").arg(path), 4000);
    });
    dialog.exec();
}

void MainWindow::onStateChanged(const QString& state)
{
    m_stateLabel->setText(state);
    updateButtonState(state);
    statusBar()->showMessage(state, 4000);
}

void MainWindow::onError(const QString& message)
{
    m_pendingSnapshot.reset();
    clearDetectionDisplay();
    updateDetectionStatus(m_displaySnapshot.get());
    m_stateLabel->setText(QStringLiteral("错误"));
    statusBar()->showMessage(message, 8000);
    QMessageBox::warning(this, QStringLiteral("监测操作失败"), message);
    updateButtonState(QStringLiteral("Stopped"));
}

void MainWindow::onReplayRequested(const QString& path)
{
    // Replay can be invoked while the monitor's source controls are disabled.
    // Invalidate the old session before changing its visible file/metadata.
    stopMonitoring();
    m_filePath->setText(path);
    if (m_sourceCombo->currentData().toInt() != static_cast<int>(algorithm::SourceKind::File)) {
        m_sourceCombo->setCurrentIndex(2);
    } else {
        applyFileMetadata(path);
    }
    clearMonitoringDisplay();
    selectMainPage(0);
    if (applyCurrentConfiguration())
        statusBar()->showMessage(QStringLiteral("已切换回放文件，可点击开始监测读取。"), 5000);
}

void MainWindow::updateRuntimeStatus()
{
    // Settle on the wall-clock timer, including empty windows after pause/EOF.
    if (m_displayRateTimer.isValid()) {
        const auto elapsedMs = m_displayRateTimer.restart();
        m_displayRateHz = elapsedMs > 0 ? 1000.0 * m_displayRateFrames / elapsedMs : 0;
        m_displayRateFrames = 0;
    }
    updateDetectionStatus(m_displaySnapshot.get());
    m_statusStrip->setTime(QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss")));
    m_statusStrip->setLoadValues(0, 0, 0);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    saveUiState();
    m_controller.stop();
    event->accept();
}

void MainWindow::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_titleBar &&
        m_titleBar->geometry().contains(event->position().toPoint()) &&
        !qobject_cast<QAbstractButton*>(childAt(event->position().toPoint()))) {
        m_dragging = true;
        m_dragOffset = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
        return;
    }
    QMainWindow::mousePressEvent(event);
}

void MainWindow::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging && !isMaximized()) {
        move(event->globalPosition().toPoint() - m_dragOffset);
        event->accept();
        return;
    }
    QMainWindow::mouseMoveEvent(event);
}

void MainWindow::mouseReleaseEvent(QMouseEvent* event)
{
    m_dragging = false;
    QMainWindow::mouseReleaseEvent(event);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (event) {
        auto* watchedWidget = qobject_cast<QWidget*>(watched);
        if (watchedWidget && watchedWidget->window() == this) {
            if (event->type() == QEvent::MouseMove) {
                auto* mouseEvent = static_cast<QMouseEvent*>(event);
                if (m_manualResizing) {
                    applyManualResize(mouseEvent->globalPosition().toPoint());
                    mouseEvent->accept();
                    return true;
                }
                updateResizeCursor(mouseEvent->globalPosition().toPoint());
            } else if (event->type() == QEvent::MouseButtonPress) {
                auto* mouseEvent = static_cast<QMouseEvent*>(event);
                if (mouseEvent->button() == Qt::LeftButton && !isMaximized() &&
                    !isFullScreen()) {
                    const Qt::Edges edges = resizeEdgesAt(
                        mouseEvent->globalPosition().toPoint());
                    if (edges != Qt::Edges()) {
                        restoreResizeCursor();
                        if (windowHandle() && windowHandle()->startSystemResize(edges)) {
                            mouseEvent->accept();
                            return true;
                        }
                        m_manualResizing = true;
                        m_resizeEdges = edges;
                        m_resizePressPosition = mouseEvent->globalPosition().toPoint();
                        m_resizeGeometry = geometry();
                        grabMouse();
                        mouseEvent->accept();
                        return true;
                    }
                }
            } else if (event->type() == QEvent::MouseButtonRelease &&
                       m_manualResizing) {
                auto* mouseEvent = static_cast<QMouseEvent*>(event);
                if (mouseEvent->button() == Qt::LeftButton) {
                    finishManualResize();
                    mouseEvent->accept();
                    return true;
                }
            } else if (event->type() == QEvent::WindowDeactivate) {
                restoreResizeCursor();
            }
        }
    }

    if (watched == m_titleBar && event) {
        switch (event->type()) {
        case QEvent::MouseButtonDblClick: {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                toggleMaximize();
                mouseEvent->accept();
                return true;
            }
            break;
        }
        case QEvent::MouseButtonPress: {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton && !isMaximized()) {
                m_dragging = true;
                m_dragOffset = mouseEvent->globalPosition().toPoint() - frameGeometry().topLeft();
                m_titleBar->grabMouse();
                mouseEvent->accept();
                return true;
            }
            break;
        }
        case QEvent::MouseMove: {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (m_dragging && !isMaximized() &&
                (mouseEvent->buttons() & Qt::LeftButton)) {
                move(mouseEvent->globalPosition().toPoint() - m_dragOffset);
                mouseEvent->accept();
                return true;
            }
            break;
        }
        case QEvent::MouseButtonRelease: {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton && m_dragging) {
                m_dragging = false;
                m_titleBar->releaseMouse();
                mouseEvent->accept();
                return true;
            }
            break;
        }
        default:
            break;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

Qt::Edges MainWindow::resizeEdgesAt(const QPoint& globalPosition) const
{
    if (isMaximized() || isFullScreen()) return {};
    const QPoint localPosition = mapFromGlobal(globalPosition);
    constexpr int border = 10;
    if (!rect().contains(localPosition)) return {};

    Qt::Edges edges;
    if (localPosition.x() < border) edges |= Qt::LeftEdge;
    if (localPosition.x() >= width() - border) edges |= Qt::RightEdge;
    if (localPosition.y() < border) edges |= Qt::TopEdge;
    if (localPosition.y() >= height() - border) edges |= Qt::BottomEdge;
    return edges;
}

void MainWindow::updateResizeCursor(const QPoint& globalPosition)
{
    const Qt::Edges edges = resizeEdgesAt(globalPosition);
    if (edges == Qt::Edges()) {
        restoreResizeCursor();
        return;
    }

    Qt::CursorShape shape = Qt::ArrowCursor;
    const bool horizontal = edges.testFlag(Qt::LeftEdge) || edges.testFlag(Qt::RightEdge);
    const bool vertical = edges.testFlag(Qt::TopEdge) || edges.testFlag(Qt::BottomEdge);
    if (horizontal && vertical) {
        const bool forwardDiagonal =
            (edges.testFlag(Qt::LeftEdge) && edges.testFlag(Qt::TopEdge)) ||
            (edges.testFlag(Qt::RightEdge) && edges.testFlag(Qt::BottomEdge));
        shape = forwardDiagonal ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor;
    } else if (horizontal) {
        shape = Qt::SizeHorCursor;
    } else if (vertical) {
        shape = Qt::SizeVerCursor;
    }

    if (!m_resizeCursorOverridden) {
        QApplication::setOverrideCursor(QCursor(shape));
        m_resizeCursorOverridden = true;
    } else {
        QApplication::changeOverrideCursor(QCursor(shape));
    }
}

void MainWindow::restoreResizeCursor()
{
    if (m_resizeCursorOverridden) {
        QApplication::restoreOverrideCursor();
        m_resizeCursorOverridden = false;
    }
}

void MainWindow::applyManualResize(const QPoint& globalPosition)
{
    if (!m_manualResizing) return;
    const QPoint delta = globalPosition - m_resizePressPosition;
    QRect nextGeometry = m_resizeGeometry;
    const int minimumWidth = std::max(1, minimumSize().width());
    const int minimumHeight = std::max(1, minimumSize().height());

    if (m_resizeEdges.testFlag(Qt::LeftEdge)) {
        nextGeometry.setLeft(std::min(m_resizeGeometry.left() + delta.x(),
                                      m_resizeGeometry.right() - minimumWidth + 1));
    }
    if (m_resizeEdges.testFlag(Qt::RightEdge)) {
        nextGeometry.setRight(std::max(m_resizeGeometry.right() + delta.x(),
                                       m_resizeGeometry.left() + minimumWidth - 1));
    }
    if (m_resizeEdges.testFlag(Qt::TopEdge)) {
        nextGeometry.setTop(std::min(m_resizeGeometry.top() + delta.y(),
                                     m_resizeGeometry.bottom() - minimumHeight + 1));
    }
    if (m_resizeEdges.testFlag(Qt::BottomEdge)) {
        nextGeometry.setBottom(std::max(m_resizeGeometry.bottom() + delta.y(),
                                        m_resizeGeometry.top() + minimumHeight - 1));
    }
    setGeometry(nextGeometry);
}

void MainWindow::finishManualResize()
{
    if (!m_manualResizing) return;
    m_manualResizing = false;
    m_resizeEdges = {};
    releaseMouse();
    restoreResizeCursor();
}

bool MainWindow::nativeEvent(const QByteArray& eventType, void* message,
                             qintptr* result)
{
#ifdef Q_OS_WIN
    if ((eventType == QByteArrayLiteral("windows_generic_MSG") ||
         eventType == QByteArrayLiteral("windows_dispatcher_MSG")) &&
        message && result && !isMaximized() && !isFullScreen()) {
        const auto* nativeMessage = static_cast<const MSG*>(message);
        if (nativeMessage->message == WM_NCHITTEST) {
            const QPoint localPosition = mapFromGlobal(QCursor::pos());
            constexpr int border = 8;
            const bool left = localPosition.x() >= 0 && localPosition.x() < border;
            const bool right = localPosition.x() >= width() - border &&
                               localPosition.x() < width();
            const bool top = localPosition.y() >= 0 && localPosition.y() < border;
            const bool bottom = localPosition.y() >= height() - border &&
                                localPosition.y() < height();

            if (top && left) *result = HTTOPLEFT;
            else if (top && right) *result = HTTOPRIGHT;
            else if (bottom && left) *result = HTBOTTOMLEFT;
            else if (bottom && right) *result = HTBOTTOMRIGHT;
            else if (left) *result = HTLEFT;
            else if (right) *result = HTRIGHT;
            else if (top) *result = HTTOP;
            else if (bottom) *result = HTBOTTOM;
            else return QMainWindow::nativeEvent(eventType, message, result);
            return true;
        }
    }
#else
    Q_UNUSED(eventType);
    Q_UNUSED(message);
    Q_UNUSED(result);
#endif
    return QMainWindow::nativeEvent(eventType, message, result);
}

} // namespace scn::app
