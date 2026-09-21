#include "MainWindow.h"
#include "../../../source/FileSource/FileSource.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QColor>
#include <QDateTime>
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

#include <algorithm>
#include <cmath>

namespace scn::app
{

namespace
{
class FrequencySpinBox final : public QDoubleSpinBox
{
public:
    explicit FrequencySpinBox(QWidget* parent)
        : QDoubleSpinBox(parent)
    {
        setSuffix(QString());
    }

protected:
    static bool parseFrequencyText(const QString& text, double& result)
    {
        QString value = text.simplified();
        value.remove(QLatin1Char(' '));
        if (value.isEmpty()) return false;

        QString lower = value.toLower();
        double multiplier = 1.0;
        const auto stripSuffix = [&lower, &value](const QString& suffix) {
            lower.chop(suffix.size());
            value.chop(suffix.size());
        };

        // ISA-style input accepts both full units and their one-letter
        // abbreviations.  Matching is deliberately case-insensitive.
        if (lower.endsWith(QStringLiteral("ghz"))) {
            stripSuffix(QStringLiteral("ghz"));
            multiplier = 1e9;
        } else if (lower.endsWith(QStringLiteral("mhz"))) {
            stripSuffix(QStringLiteral("mhz"));
            multiplier = 1e6;
        } else if (lower.endsWith(QStringLiteral("khz"))) {
            stripSuffix(QStringLiteral("khz"));
            multiplier = 1e3;
        } else if (lower.endsWith(QStringLiteral("hz"))) {
            stripSuffix(QStringLiteral("hz"));
        } else if (lower.endsWith(QLatin1Char('g'))) {
            stripSuffix(QStringLiteral("g"));
            multiplier = 1e9;
        } else if (lower.endsWith(QLatin1Char('m'))) {
            stripSuffix(QStringLiteral("m"));
            multiplier = 1e6;
        } else if (lower.endsWith(QLatin1Char('k'))) {
            stripSuffix(QStringLiteral("k"));
            multiplier = 1e3;
        } else if (lower.endsWith(QLatin1Char('h'))) {
            stripSuffix(QStringLiteral("h"));
        }

        bool ok = false;
        const double numeric = value.toDouble(&ok);
        if (!ok || !std::isfinite(numeric)) return false;
        result = numeric * multiplier;
        return std::isfinite(result);
    }

    QString textFromValue(double value) const override
    {
        const double absHz = std::abs(value);
        if (absHz >= 1e9)
            return QStringLiteral("%1 GHz").arg(value / 1e9, 0, 'f', 9);
        if (absHz >= 1e6)
            return QStringLiteral("%1 MHz").arg(value / 1e6, 0, 'f', 6);
        if (absHz >= 1e3)
            return QStringLiteral("%1 kHz").arg(value / 1e3, 0, 'f', 3);
        return QStringLiteral("%1 Hz").arg(value, 0, 'f', 0);
    }

    double valueFromText(const QString& text) const override
    {
        double result = 0.0;
        return parseFrequencyText(text, result) ? result : QDoubleSpinBox::value();
    }

    QValidator::State validate(QString& input, int& position) const override
    {
        Q_UNUSED(position)
        if (input.trimmed().isEmpty()) return QValidator::Intermediate;

        double result = 0.0;
        if (!parseFrequencyText(input, result)) return QValidator::Invalid;
        return result >= minimum() && result <= maximum()
            ? QValidator::Acceptable : QValidator::Invalid;
    }
};

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
            });
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
    connect(m_settingsPage, &SettingsPage::detectionApplyRequested,
            this, [this] { (void)applyDetectionConfiguration(); });
    // ISA 中频谱图是频率视图的交互主控，瀑布图跟随同一范围和选中频点重算。
    connect(m_spectrum, &SpectrumWidget::viewRangeChanged,
            m_waterfall, &WaterfallWidget::setFrequencyView);
    connect(m_spectrum, &SpectrumWidget::frequencySelected,
            m_waterfall, &WaterfallWidget::setSelectedFrequency);

    m_statusTimer = new QTimer(this);
    m_statusTimer->setInterval(1000);
    connect(m_statusTimer, &QTimer::timeout, this, &MainWindow::updateRuntimeStatus);
    m_statusTimer->start();

    m_displayTimer = new QTimer(this);
    m_displayTimer->setInterval(33);
    connect(m_displayTimer, &QTimer::timeout, this, &MainWindow::refreshDisplay);
    m_displayTimer->start();

    loadUiState();
    updateSourceControls();
    updateDisplayDomain();
    (void)applyDetectionConfiguration();
    applyConfiguration();
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
    auto* layout = new QHBoxLayout(m_titleBar);
    layout->setContentsMargins(12, 0, 0, 0);
    layout->setSpacing(0);

    auto* logo = new QLabel(m_titleBar);
    logo->setFixedSize(30, 30);
    logo->setPixmap(QPixmap(QStringLiteral(":/title/title_log.png"))
        .scaled(30, 30, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    layout->addWidget(logo);
    layout->addSpacing(6);
    auto* title = label(QStringLiteral("智能频谱监测仪"), m_titleBar, QStringLiteral("appTitle"));
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
    plotLayout->setSpacing(2);
    m_plotSplitter = new QSplitter(Qt::Vertical, plotPanel);
    auto* splitter = m_plotSplitter;
    m_waterfall = new WaterfallWidget(splitter);
    m_spectrum = new SpectrumWidget(splitter);
    m_waterfall->setMinimumHeight(135);
    m_spectrum->setMinimumHeight(280);
    splitter->addWidget(m_waterfall);
    splitter->addWidget(m_spectrum);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 5);
    splitter->setSizes({190, 500});
    plotLayout->addWidget(splitter);
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

    m_sourceFields = new QStackedWidget(left);
    m_sourceFields->setMinimumWidth(420);
    m_sourceFields->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto* hardwarePage = new QWidget(m_sourceFields);
    auto* hardwareLayout = new QHBoxLayout(hardwarePage);
    hardwareLayout->setContentsMargins(0, 0, 0, 0);
    hardwareLayout->setSpacing(8);
    auto* centerLabel = label(QStringLiteral("中心频率"), hardwarePage, QStringLiteral("controlLabel"));
    centerLabel->setMinimumWidth(64);
    hardwareLayout->addWidget(centerLabel);
    m_centerFrequency = new FrequencySpinBox(hardwarePage);
    m_centerFrequency->setRange(0.0, 6.4e9);
    m_centerFrequency->setDecimals(6);
    m_centerFrequency->setValue(2.4e9);
    m_centerFrequency->setMinimumWidth(150);
    hardwareLayout->addWidget(m_centerFrequency, 1);
    auto* bandwidthLabel = label(QStringLiteral("扫宽"), hardwarePage, QStringLiteral("controlLabel"));
    bandwidthLabel->setMinimumWidth(36);
    hardwareLayout->addWidget(bandwidthLabel);
    m_bandwidth = new FrequencySpinBox(hardwarePage);
    m_bandwidth->setRange(20.0, 6.4e9);
    m_bandwidth->setDecimals(6);
    m_bandwidth->setValue(100.0e6);
    m_bandwidth->setMinimumWidth(150);
    hardwareLayout->addWidget(m_bandwidth, 1);
    hardwareLayout->addStretch(1);
    m_sourceFields->addWidget(hardwarePage);

    auto* filePage = new QWidget(m_sourceFields);
    auto* fileLayout = new QHBoxLayout(filePage);
    fileLayout->setContentsMargins(0, 0, 0, 0);
    fileLayout->setSpacing(8);
    auto* fileLabel = label(QStringLiteral("文件路径"), filePage, QStringLiteral("controlLabel"));
    fileLabel->setMinimumWidth(52);
    fileLayout->addWidget(fileLabel);
    m_filePath = new QLineEdit(filePage);
    m_filePath->setPlaceholderText(QStringLiteral("请选择回放文件..."));
    m_filePath->setMinimumWidth(300);
    m_filePath->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    fileLayout->addWidget(m_filePath, 1);
    m_browseButton = new QPushButton(filePage);
    m_browseButton->setIcon(QIcon(QStringLiteral(":/button/folder.png")));
    m_browseButton->setIconSize(QSize(16, 16));
    m_browseButton->setObjectName(QStringLiteral("pageToolButton"));
    m_browseButton->setToolTip(QStringLiteral("选择频谱文件"));
    m_browseButton->setFixedSize(34, 30);
    fileLayout->addWidget(m_browseButton);
    m_sourceFields->addWidget(filePage);
    configGrid->addWidget(m_sourceFields, 0, 2, 1, 5);

    configGrid->addWidget(label(QStringLiteral("带宽分辨率"), left, QStringLiteral("controlLabel")), 0, 7);
    m_resolutionBandwidth = new FrequencySpinBox(left);
    m_resolutionBandwidth->setRange(0.602006912, 10.1e6);
    m_resolutionBandwidth->setDecimals(6);
    m_resolutionBandwidth->setValue(50.0e3);
    m_resolutionBandwidth->setMinimumWidth(130);
    configGrid->addWidget(m_resolutionBandwidth, 0, 8);

    // ISA keeps start/stop frequency visible in both live and file modes.
    // These controls use Hz internally and accept an explicit unit suffix.
    configGrid->addWidget(label(QStringLiteral("起始频率"), left, QStringLiteral("controlLabel")), 1, 0);
    m_startFrequency = new FrequencySpinBox(left);
    m_startFrequency->setRange(0.0, 6.4e9);
    m_startFrequency->setDecimals(6);
    m_startFrequency->setValue(2.35e9);
    m_startFrequency->setMinimumWidth(150);
    configGrid->addWidget(m_startFrequency, 1, 1);
    configGrid->addWidget(label(QStringLiteral("终止频率"), left, QStringLiteral("controlLabel")), 1, 2);
    m_endFrequency = new FrequencySpinBox(left);
    m_endFrequency->setRange(0.0, 6.4e9);
    m_endFrequency->setDecimals(6);
    m_endFrequency->setValue(2.45e9);
    m_endFrequency->setMinimumWidth(150);
    configGrid->addWidget(m_endFrequency, 1, 3);

    configGrid->addWidget(label(QStringLiteral("参考电平"), left, QStringLiteral("controlLabel")), 1, 4);
    m_referenceLevel = new QDoubleSpinBox(left);
    m_referenceLevel->setRange(-1000.0, 1000.0);
    m_referenceLevel->setDecimals(1);
    m_referenceLevel->setValue(-25.0);
    m_referenceLevel->setSuffix(QStringLiteral(" dBm"));
    m_referenceLevel->setMinimumWidth(120);
    m_referenceLevel->setToolTip(QStringLiteral(
        "用于自动增益/衰减控制；频谱图和瀑布图显示范围为参考电平以下100 dB"));
    configGrid->addWidget(m_referenceLevel, 1, 5);
    m_rbwShapeLabel = label(QStringLiteral("RBW窗口"), left, QStringLiteral("controlLabel"));
    configGrid->addWidget(m_rbwShapeLabel, 1, 6);
    m_rbwShape = new QComboBox(left);
    m_rbwShape->addItem(QStringLiteral("Nuttall"), static_cast<int>(source::RbwShape::Nuttall));
    m_rbwShape->addItem(QStringLiteral("Flattop"), static_cast<int>(source::RbwShape::Flattop));
    m_rbwShape->addItem(QStringLiteral("CISPR"), static_cast<int>(source::RbwShape::Cispr));
    m_rbwShape->setToolTip(QStringLiteral(
        "BB60C RBW窗口：Nuttall速度优先，Flattop幅度精度优先，CISPR为6 dB截止"));
    m_rbwShape->setMinimumWidth(130);
    configGrid->addWidget(m_rbwShape, 1, 7);
    configGrid->setColumnStretch(1, 1);
    configGrid->setColumnStretch(3, 1);
    configGrid->setColumnStretch(5, 1);
    configGrid->setColumnStretch(8, 1);
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
    operationPanel->setMinimumWidth(260);
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
    m_startButton->setFixedSize(120, 40);
    operation->addWidget(m_startButton);
    m_pauseButton = new QPushButton(QStringLiteral("暂停查看"), panel);
    m_pauseButton->setObjectName(QStringLiteral("dangerButton"));
    m_pauseButton->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
    m_pauseButton->setFixedSize(120, 40);
    m_pauseButton->setEnabled(false);
    operation->addWidget(m_pauseButton);
    operation->addStretch(1);
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
    auto* critical = metricCard(QStringLiteral(":/collect/critical_alert.png"), QStringLiteral("严重警告\n未接入"),
                                QStringLiteral("#E63E3E"), m_criticalAlertLabel, metricsPanel);
    auto* general = metricCard(QStringLiteral(":/collect/general_alarm.png"), QStringLiteral("一般警告\n未接入"),
                               QStringLiteral("#FFBA00"), m_generalAlarmLabel, metricsPanel);
    auto* total = metricCard(QStringLiteral(":/collect/signal_total.png"), QStringLiteral("信号总数"),
                             QStringLiteral("#0A8CFE"), m_signalTotalLabel, metricsPanel);
    m_criticalAlertLabel->setText(QStringLiteral("—"));
    m_generalAlarmLabel->setText(QStringLiteral("—"));
    critical->setToolTip(QStringLiteral("告警规则未接入；SCN 置信度不代表告警等级。"));
    general->setToolTip(critical->toolTip());
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

    const auto syncFromCenterSpan = [this](double) {
        if (m_updatingFrequency) return;
        m_updatingFrequency = true;
        const double centerHz = m_centerFrequency->value();
        const double spanHz = m_bandwidth->value();
        m_startFrequency->setValue(centerHz - spanHz / 2.0);
        m_endFrequency->setValue(centerHz + spanHz / 2.0);
        m_updatingFrequency = false;
    };
    const auto syncFromStartEnd = [this](double) {
        if (m_updatingFrequency) return;
        m_updatingFrequency = true;
        const double startHz = m_startFrequency->value();
        const double endHz = m_endFrequency->value();
        m_centerFrequency->setValue((startHz + endHz) / 2.0);
        m_bandwidth->setValue(std::max(20.0, endHz - startHz));
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
    m_signalTable->setHorizontalHeaderLabels({QStringLiteral("ID"), QStringLiteral("中心频率(MHz)"),
        QStringLiteral("带宽(kHz)"), QStringLiteral("信号类型"), QStringLiteral("告警等级"),
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
        const double startHz = m_startFrequency->value();
        const double endHz = m_endFrequency->value();
        config.centerFrequencyHz = (startHz + endHz) / 2.0;
        config.bandwidthHz = endHz - startHz;
    } else {
        config.centerFrequencyHz = m_centerFrequency->value();
        config.bandwidthHz = std::max(20.0, m_bandwidth->value());
    }
    config.resolutionBandwidthHz = m_resolutionBandwidth->value();
    config.referenceLevelDbm = m_referenceLevel->value();
    config.rbwShape = static_cast<source::RbwShape>(m_rbwShape->currentData().toInt());
    config.pointCount = static_cast<std::size_t>(m_pointCount->value());
    config.frameRateHz = m_frameRate->value();
    config.loopFile = m_loopFile->isChecked();
    return config;
}

QString MainWindow::formatFrequency(double hz, int decimals) const
{
    const double absHz = std::abs(hz);
    if (absHz >= 1e9)
        return QStringLiteral("%1 GHz").arg(hz / 1e9, 0, 'f', decimals);
    if (absHz >= 1e6)
        return QStringLiteral("%1 MHz").arg(hz / 1e6, 0, 'f', decimals);
    if (absHz >= 1e3)
        return QStringLiteral("%1 kHz").arg(hz / 1e3, 0, 'f', decimals);
    return QStringLiteral("%1 Hz").arg(hz, 0, 'f', decimals);
}

bool MainWindow::validateConfiguration(const source::SourceConfig& config, QString& error) const
{
    if (config.kind == algorithm::SourceKind::File) {
        if (config.filePath.empty()) {
            error = QStringLiteral("请选择频谱文件。");
            return false;
        }
        if (!std::isfinite(config.centerFrequencyHz) ||
            !std::isfinite(config.bandwidthHz) || config.bandwidthHz <= 0.0) {
            error = QStringLiteral("文件源的起始频率必须小于终止频率。");
            return false;
        }
        return true;
    }

    const double startHz = config.centerFrequencyHz - config.bandwidthHz / 2.0;
    const double endHz = config.centerFrequencyHz + config.bandwidthHz / 2.0;
    if (!std::isfinite(startHz) || !std::isfinite(endHz) ||
        startHz < 9.0e3 || endHz > 6.0e9 || endHz <= startHz) {
        error = QStringLiteral("起止频率必须位于 9 kHz 至 6 GHz 范围内。");
        return false;
    }
    if (config.bandwidthHz < 20.0) {
        error = QStringLiteral("扫宽不能小于 20 Hz。");
        return false;
    }
    if (!std::isfinite(config.resolutionBandwidthHz) ||
        config.resolutionBandwidthHz < 0.602006912 ||
        config.resolutionBandwidthHz > 10.1e6) {
        error = QStringLiteral("RBW 必须位于 0.602006912 Hz 至 10.1 MHz 范围内。");
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
        const double startHz = metadata.centerFrequencyHz - metadata.bandwidthHz / 2.0;
        const double endHz = metadata.centerFrequencyHz + metadata.bandwidthHz / 2.0;
        m_startFrequency->setValue(startHz);
        m_endFrequency->setValue(endHz);
        m_centerFrequency->setValue(metadata.centerFrequencyHz);
        m_bandwidth->setValue(metadata.bandwidthHz);
        m_updatingFrequency = false;
        m_fileFrequencyMetadataLocked = true;
    }
    if (metadata.hasResolutionBandwidth) {
        m_resolutionBandwidth->setValue(metadata.resolutionBandwidthHz);
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
    if (m_sourceFields) m_sourceFields->setCurrentIndex(fileSource ? 1 : 0);
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
}

void MainWindow::updateSourceControls()
{
    if (!m_sourceCombo) return;
    const bool fileSource = m_sourceCombo->currentData().toInt() == static_cast<int>(algorithm::SourceKind::File);
    const bool editable = !m_monitoring;
    m_centerFrequency->setEnabled(!fileSource && editable);
    m_bandwidth->setEnabled(!fileSource && editable);
    m_filePath->setEnabled(fileSource && editable);
    m_browseButton->setEnabled(fileSource && editable);
    // FILE metadata are authoritative for the active file.  Keep the values
    // visible for ISA-style inspection, but never allow editing them in FILE
    // mode; a different file is selected through the Browse button.
    m_startFrequency->setEnabled(editable && !fileSource);
    m_endFrequency->setEnabled(editable && !fileSource);
    m_resolutionBandwidth->setEnabled(editable && !fileSource);
    m_referenceLevel->setEnabled(editable && !fileSource);
    m_rbwShape->setEnabled(!fileSource && editable);
    if (m_rbwShapeLabel) m_rbwShapeLabel->setVisible(!fileSource);
    m_rbwShape->setVisible(!fileSource);
    if (auto* grid = qobject_cast<QGridLayout*>(m_sourceFields
                                                     ? m_sourceFields->parentWidget()->layout()
                                                     : nullptr)) {
        grid->invalidate();
        grid->activate();
    }
    m_loopFile->setEnabled(fileSource && editable);
    m_sourceCombo->setEnabled(editable);
    m_pointCount->setEnabled(editable);
    m_frameRate->setEnabled(editable);
    m_applyButton->setEnabled(editable);
}

void MainWindow::updateDisplayDomain()
{
    if (!m_spectrum || !m_waterfall || !m_startFrequency || !m_endFrequency ||
        !m_referenceLevel) {
        return;
    }
    const double startHz = m_startFrequency->value();
    const double endHz = m_endFrequency->value();
    const double referenceLevelDbm = m_referenceLevel->value();
    if (!std::isfinite(startHz) || !std::isfinite(endHz) || !(endHz > startHz)) {
        return;
    }
    m_spectrum->setDisplayDomain(startHz, endHz, referenceLevelDbm);
    m_waterfall->setDisplayDomain(startHz, endHz, referenceLevelDbm);
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
                                            double fallback,
                                            double legacyScale) {
        if (settings.contains(hzKey)) return settings.value(hzKey).toDouble();
        return settings.value(legacyKey, fallback / legacyScale).toDouble() * legacyScale;
    };
    m_centerFrequency->setValue(readFrequency(QStringLiteral("source/centerFrequencyHz"),
                                               QStringLiteral("source/centerFrequencyGHz"),
                                               m_centerFrequency->value(), 1.0e9));
    m_bandwidth->setValue(readFrequency(QStringLiteral("source/bandwidthHz"),
                                        QStringLiteral("source/bandwidthGHz"),
                                        m_bandwidth->value(), 1.0e9));
    m_startFrequency->setValue(readFrequency(QStringLiteral("source/startFrequencyHz"),
                                             QStringLiteral("source/startFrequencyGHz"),
                                             m_startFrequency->value(), 1.0e9));
    m_endFrequency->setValue(readFrequency(QStringLiteral("source/endFrequencyHz"),
                                           QStringLiteral("source/endFrequencyGHz"),
                                           m_endFrequency->value(), 1.0e9));
    m_resolutionBandwidth->setValue(readFrequency(QStringLiteral("source/resolutionBandwidthHz"),
                                                  QStringLiteral("source/resolutionBandwidthKHz"),
                                                  m_resolutionBandwidth->value(), 1.0e3));
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

    const int page = std::clamp(settings.value(QStringLiteral("ui/mainPage"), 0).toInt(),
                                0, std::max(0, m_pages->count() - 1));
    selectMainPage(page);
}

void MainWindow::saveUiState() const
{
    QSettings settings(QStringLiteral("SCN"), QStringLiteral("HaiAISpecMonitor"));
    settings.setValue(QStringLiteral("ui/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("ui/windowState"), saveState(1));
    settings.setValue(QStringLiteral("ui/mainPage"), m_pages ? m_pages->currentIndex() : 0);
    if (m_plotSplitter) {
        settings.setValue(QStringLiteral("ui/plotSplitterState"), m_plotSplitter->saveState());
    }

    const int sourceKind = m_sourceCombo->currentData().toInt();
    settings.setValue(QStringLiteral("source/kind"), sourceKind);
    settings.setValue(QStringLiteral("source/filePath"), m_filePath->text());
    settings.setValue(QStringLiteral("source/centerFrequencyHz"), m_centerFrequency->value());
    settings.setValue(QStringLiteral("source/bandwidthHz"), m_bandwidth->value());
    settings.setValue(QStringLiteral("source/startFrequencyHz"), m_startFrequency->value());
    settings.setValue(QStringLiteral("source/endFrequencyHz"), m_endFrequency->value());
    settings.setValue(QStringLiteral("source/resolutionBandwidthHz"), m_resolutionBandwidth->value());
    settings.setValue(QStringLiteral("source/referenceLevelDbm"), m_referenceLevel->value());
    settings.setValue(QStringLiteral("source/rbwShape"), m_rbwShape->currentData().toInt());
    settings.setValue(QStringLiteral("source/pointCount"), m_pointCount->value());
    settings.setValue(QStringLiteral("source/frameRate"), m_frameRate->value());
    settings.setValue(QStringLiteral("source/loopFile"), m_loopFile->isChecked());
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
    QString validationError;
    if (!validateConfiguration(config, validationError)) {
        statusBar()->showMessage(validationError, 6000);
        return false;
    }
    // SCN controls are a draft. Only their explicit Apply action changes the
    // accepted detector configuration; source start must not commit that draft.
    // Session.configure creates a new generation synchronously. Reject already
    // published snapshots from the preceding run without waiting for Running.
    m_minimumGeneration = m_latestGeneration + 1;
    m_pendingSnapshot.reset();
    m_controller.configure(config);
    updateDisplayDomain();
    m_spectrum->resetView();
    m_waterfall->resetView();
    m_statusStrip->setMenuInfo(config.centerFrequencyHz, config.bandwidthHz,
                               config.resolutionBandwidthHz);
    m_statusStrip->setMenuInfoVisible(config.kind == algorithm::SourceKind::File);
    statusBar()->showMessage(QStringLiteral("参数已提交，等待数据源初始化。"), 3000);
    return true;
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
    m_criticalAlertLabel->setText(QStringLiteral("—"));
    m_generalAlarmLabel->setText(QStringLiteral("—"));
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
    m_lastDetectionKey.reset();
    m_spectrum->clear();
    m_waterfall->clear();
    m_displayRateTimer.invalidate();
    m_displayRateFrames = 0;
    m_displayRateHz = 0;
    m_signalTable->setRowCount(0);
    m_criticalAlertLabel->setText(QStringLiteral("—"));
    m_generalAlarmLabel->setText(QStringLiteral("—"));
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
    if (!snapshot) return;
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
    m_frameLabel->setText(QStringLiteral("帧号：%1").arg(snapshot->frame.sequence));
    const bool fileSource = static_cast<algorithm::SourceKind>(m_sourceCombo->currentData().toInt()) ==
        algorithm::SourceKind::File;
    if (fileSource) {
        m_statusStrip->setMenuInfo((snapshot->frame.startFrequencyHz + endFrequencyHz) / 2.0,
                                   endFrequencyHz - snapshot->frame.startFrequencyHz,
                                   m_resolutionBandwidth->value());
    } else {
        m_statusStrip->setMenuInfoVisible(false);
    }
    updateDetectionStatus(snapshot.get());
    const auto& data = snapshot->detection;
    const DetectionKey key{data.generation, data.configVersion, data.sequence, data.stage};
    if (!m_lastDetectionKey || *m_lastDetectionKey != key) {
        m_lastDetectionKey = key;
        updateMonitorMetrics(*snapshot);
        updateSignalTable(*snapshot);
        if (fileSource) {
            m_playbackPage->rememberFile(m_filePath->text(), m_sourceCombo->currentText(),
                snapshot->frame.startFrequencyHz, endFrequencyHz, m_resolutionBandwidth->value(),
                hasDetectionObservations(data) ? static_cast<int>(data.detections.size()) : 0, 0);
        }
    }
}

void MainWindow::updateMonitorMetrics(const algorithm::DisplaySnapshot& snapshot)
{
    m_criticalAlertLabel->setText(QStringLiteral("—"));
    m_generalAlarmLabel->setText(QStringLiteral("—"));
    const auto count = hasDetectionObservations(snapshot.detection)
        ? snapshot.detection.detections.size() : 0;
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

QString MainWindow::signalDetails(const algorithm::DetectedSignal& signal, bool fileSource) const
{
    QString branch;
    switch (signal.branch) {
    case algorithm::SpectrumBranch::Average: branch = QStringLiteral("平均谱（Average）"); break;
    case algorithm::SpectrumBranch::Maximum: branch = QStringLiteral("最大谱（Maximum）"); break;
    case algorithm::SpectrumBranch::Both: branch = QStringLiteral("双分支融合（Both）"); break;
    }
    const QStringList details{
        QStringLiteral("信号 ID：%1").arg(signal.id),
        QStringLiteral("起始频率：%1 Hz").arg(signal.startFrequencyHz, 0, 'f', 3),
        QStringLiteral("终止频率：%1 Hz").arg(signal.endFrequencyHz, 0, 'f', 3),
        QStringLiteral("中心频率：%1 Hz").arg(signal.centerFrequencyHz, 0, 'f', 3),
        QStringLiteral("带宽：%1 Hz").arg(signal.bandwidthHz, 0, 'f', 3),
        QStringLiteral("置信度：%1").arg(signal.confidence, 0, 'f', 6),
        QStringLiteral("CNR（snrDb）：%1 dB").arg(signal.snrDb, 0, 'f', 3),
        QStringLiteral("信号电平：%1 dBm").arg(signal.signalLevelDbm, 0, 'f', 3),
        QStringLiteral("噪声电平：%1 dBm").arg(signal.noiseLevelDbm, 0, 'f', 3),
        QStringLiteral("检测分支：%1").arg(branch),
        QStringLiteral("首次出现：%1").arg(formatDetectionTime(signal.firstSeenNs, fileSource)),
        QStringLiteral("最近出现：%1").arg(formatDetectionTime(signal.lastSeenNs, fileSource)),
        QStringLiteral("firstSeenNs：%1 | lastSeenNs：%2").arg(signal.firstSeenNs).arg(signal.lastSeenNs),
        QStringLiteral("出现次数：%1").arg(signal.occurrenceCount),
        QStringLiteral("信号类型：未分类 | 告警等级：—（未接入）"),
        fileSource ? QStringLiteral("回放时间由文件帧位置与帧率生成，与实际播放速度无关。")
                   : QStringLiteral("硬件时间相对本轮首个显示帧；负值表示更早的观测，不是日历时间。")};
    return details.join(QLatin1Char('\n'));
}

void MainWindow::updateSignalTable(const algorithm::DisplaySnapshot& snapshot)
{
    const auto& data = snapshot.detection;
    const bool fileSource = m_sourceCombo->currentData().toInt() == static_cast<int>(algorithm::SourceKind::File);
    const int count = hasDetectionObservations(data) ? static_cast<int>(data.detections.size()) : 0;
    m_signalTable->setRowCount(count);
    for (int row = 0; row < count; ++row) {
        const auto& signal = data.detections.at(static_cast<std::size_t>(row));
        auto* id = tableItem(QString::number(signal.id));
        const auto details = signalDetails(signal, fileSource);
        id->setData(Qt::UserRole, details);
        id->setToolTip(details);
        m_signalTable->setItem(row, 0, id);
        m_signalTable->setItem(row, 1, tableItem(QString::number(signal.centerFrequencyHz / 1e6, 'f', 3)));
        m_signalTable->setItem(row, 2, tableItem(QString::number(signal.bandwidthHz / 1e3, 'f', 3)));
        m_signalTable->setItem(row, 3, tableItem(QStringLiteral("未分类")));
        m_signalTable->setItem(row, 4, tableItem(QStringLiteral("—")));
        auto* lastSeen = tableItem(formatDetectionTime(signal.lastSeenNs, fileSource));
        lastSeen->setToolTip(details);
        m_signalTable->setItem(row, 5, lastSeen);
        m_signalTable->setItem(row, 6, tableItem(QString::number(signal.occurrenceCount)));
    }
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

} // namespace scn::app
