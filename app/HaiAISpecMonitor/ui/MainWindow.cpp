#include "MainWindow.h"
#include "../../../source/FileSource/FileSource.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
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
#include <QStackedWidget>
#include <QStyle>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

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
    QString textFromValue(double value) const override
    {
        const double hz = value * 1e9;
        if (std::abs(hz) >= 1e9)
            return QStringLiteral("%1 GHz").arg(hz / 1e9, 0, 'f', 9);
        if (std::abs(hz) >= 1e6)
            return QStringLiteral("%1 MHz").arg(hz / 1e6, 0, 'f', 6);
        return QStringLiteral("%1 kHz").arg(hz / 1e3, 0, 'f', 3);
    }

    double valueFromText(const QString& text) const override
    {
        QString value = text.trimmed();
        double multiplier = 1e9;
        if (value.endsWith(QStringLiteral("GHz"), Qt::CaseInsensitive)) {
            value.chop(3);
            multiplier = 1e9;
        } else if (value.endsWith(QStringLiteral("MHz"), Qt::CaseInsensitive)) {
            value.chop(3);
            multiplier = 1e6;
        } else if (value.endsWith(QStringLiteral("kHz"), Qt::CaseInsensitive)) {
            value.chop(3);
            multiplier = 1e3;
        }
        bool ok = false;
        const double numeric = value.trimmed().toDouble(&ok);
        return ok ? numeric * multiplier / 1e9 : QDoubleSpinBox::value();
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
    card->setMinimumWidth(138);
    auto* layout = new QHBoxLayout(card);
    layout->setContentsMargins(12, 8, 12, 8);
    auto* iconLabel = new QLabel(card);
    iconLabel->setObjectName(QStringLiteral("metricIcon"));
    iconLabel->setPixmap(QPixmap(iconPath).scaled(36, 36, Qt::KeepAspectRatio,
                                                    Qt::SmoothTransformation));
    iconLabel->setStyleSheet(QStringLiteral("background:%1; border-radius:7px;").arg(color));
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setFixedSize(38, 38);
    auto* textLayout = new QVBoxLayout;
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(0);
    auto* titleLabel = new QLabel(title, card);
    titleLabel->setObjectName(QStringLiteral("metricTitle"));
    value = new QLabel(QStringLiteral("0"), card);
    value->setObjectName(QStringLiteral("metricValue"));
    value->setStyleSheet(QStringLiteral("color:%1;").arg(color));
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
    // ISA 中频谱图与瀑布图共享频率视图和选中标记；任一图上的操作都同步到另一幅图。
    connect(m_spectrum, &SpectrumWidget::viewRangeChanged,
            m_waterfall, &WaterfallWidget::setFrequencyView);
    connect(m_waterfall, &WaterfallWidget::viewRangeChanged,
            m_spectrum, &SpectrumWidget::setFrequencyView);
    connect(m_spectrum, &SpectrumWidget::frequencySelected,
            m_waterfall, &WaterfallWidget::setSelectedFrequency);
    connect(m_waterfall, &WaterfallWidget::frequencySelected,
            m_spectrum, &SpectrumWidget::setSelectedFrequency);

    m_statusTimer = new QTimer(this);
    m_statusTimer->setInterval(1000);
    connect(m_statusTimer, &QTimer::timeout, this, &MainWindow::updateRuntimeStatus);
    m_statusTimer->start();

    m_displayTimer = new QTimer(this);
    m_displayTimer->setInterval(33);
    connect(m_displayTimer, &QTimer::timeout, this, &MainWindow::refreshDisplay);
    m_displayTimer->start();

    const QString defaultSpectrumFile = QStringLiteral(
        R"(D:\project\isa\bin\data\spectrum_data_org\20260911_152444_651_Fc=2025000000_Bw=3950000000_Rbw=50000_Reflevel=-20.0_SpectrumLen=202242.dat)");
    m_sourceCombo->setCurrentIndex(2);
    m_filePath->setText(defaultSpectrumFile);
    applyFileMetadata(defaultSpectrumFile);
    updateSourceControls();
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
        QLabel#metricIcon { border-radius: 7px; }
        QLabel#metricTitle { color: #8b99a9; font-size: 11px; }
        QLabel#metricValue { font-size: 24px; font-weight: 700; }
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
    auto* splitter = new QSplitter(Qt::Vertical, plotPanel);
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
    panel->setMinimumHeight(82);
    auto* layout = new QHBoxLayout(panel);
    layout->setContentsMargins(12, 7, 12, 7);
    layout->setSpacing(12);

    auto* left = new QWidget(panel);
    auto* grid = new QGridLayout(left);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(4);

    grid->addWidget(label(QStringLiteral("文件路径"), panel, QStringLiteral("controlLabel")), 0, 0);
    m_filePath = new QLineEdit(panel);
    m_filePath->setPlaceholderText(QStringLiteral("请选择回放文件..."));
    grid->addWidget(m_filePath, 0, 1, 1, 5);
    m_browseButton = new QPushButton(panel);
    m_browseButton->setIcon(QIcon(QStringLiteral(":/button/folder.png")));
    m_browseButton->setIconSize(QSize(16, 16));
    m_browseButton->setObjectName(QStringLiteral("pageToolButton"));
    m_browseButton->setToolTip(QStringLiteral("选择频谱文件"));
    m_browseButton->setFixedSize(34, 30);
    grid->addWidget(m_browseButton, 0, 6);
    grid->addWidget(label(QStringLiteral("数据源"), panel, QStringLiteral("controlLabel")), 0, 7);
    m_sourceCombo = new QComboBox(panel);
    m_sourceCombo->addItem(QStringLiteral("BB60C"), static_cast<int>(algorithm::SourceKind::BB60C));
    m_sourceCombo->addItem(QStringLiteral("Harogic"), static_cast<int>(algorithm::SourceKind::Harogic));
    m_sourceCombo->addItem(QStringLiteral("FILE"), static_cast<int>(algorithm::SourceKind::File));
    m_sourceCombo->setFixedWidth(92);
    grid->addWidget(m_sourceCombo, 0, 8);

    grid->addWidget(label(QStringLiteral("起始频率"), panel, QStringLiteral("controlLabel")), 1, 0);
    m_startFrequency = new FrequencySpinBox(panel);
    m_startFrequency->setRange(0.001, 100.0);
    m_startFrequency->setDecimals(6);
    m_startFrequency->setValue(2.35);
    grid->addWidget(m_startFrequency, 1, 1);
    grid->addWidget(label(QStringLiteral("终止频率"), panel, QStringLiteral("controlLabel")), 1, 2);
    m_endFrequency = new FrequencySpinBox(panel);
    m_endFrequency->setRange(0.002, 100.0);
    m_endFrequency->setDecimals(6);
    m_endFrequency->setValue(2.45);
    grid->addWidget(m_endFrequency, 1, 3);
    grid->addWidget(label(QStringLiteral("带宽分辨率"), panel, QStringLiteral("controlLabel")), 1, 4);
    m_resolutionBandwidth = new QDoubleSpinBox(panel);
    m_resolutionBandwidth->setRange(0.001, 10000.0);
    m_resolutionBandwidth->setDecimals(3);
    m_resolutionBandwidth->setValue(50.0);
    m_resolutionBandwidth->setSuffix(QStringLiteral(" kHz"));
    grid->addWidget(m_resolutionBandwidth, 1, 5);
    grid->addWidget(label(QStringLiteral("参考电平"), panel, QStringLiteral("controlLabel")), 1, 6);
    m_referenceLevel = new QDoubleSpinBox(panel);
    m_referenceLevel->setRange(-200.0, 30.0);
    m_referenceLevel->setDecimals(1);
    m_referenceLevel->setValue(-25.0);
    m_referenceLevel->setSuffix(QStringLiteral(" dBm"));
    grid->addWidget(m_referenceLevel, 1, 7, 1, 2);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(3, 1);
    grid->setColumnStretch(5, 1);
    layout->addWidget(left, 1);

    auto* operation = new QHBoxLayout;
    operation->setContentsMargins(0, 0, 0, 0);
    operation->setSpacing(8);
    m_applyButton = new QPushButton(QStringLiteral("应用参数"), panel);
    m_applyButton->setObjectName(QStringLiteral("pageToolButton"));
    m_applyButton->setVisible(false);
    operation->addWidget(m_applyButton);
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
    layout->addLayout(operation);

    auto* critical = metricCard(QStringLiteral(":/collect/critical_alert.png"), QStringLiteral("严重警告"),
                                QStringLiteral("#E63E3E"), m_criticalAlertLabel, panel);
    auto* general = metricCard(QStringLiteral(":/collect/general_alarm.png"), QStringLiteral("一般警告"),
                               QStringLiteral("#FFBA00"), m_generalAlarmLabel, panel);
    auto* total = metricCard(QStringLiteral(":/collect/signal_total.png"), QStringLiteral("信号总数"),
                             QStringLiteral("#0A8CFE"), m_signalTotalLabel, panel);
    layout->addWidget(critical);
    layout->addWidget(general);
    layout->addWidget(total);

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
}

void MainWindow::buildSignalTable(QWidget* parent)
{
    auto* panel = new QFrame(parent);
    panel->setObjectName(QStringLiteral("signalPanel"));
    panel->setMinimumHeight(218);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(12, 2, 12, 2);

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
        QMessageBox::information(this, QStringLiteral("信号详情"),
            QStringLiteral("信号 ID：%1\n\n检测引擎当前处于接口模式，详细识别结果将在算法实现接入后显示。")
                .arg(m_signalTable->item(row, 0)->text()));
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
    const double startHz = m_startFrequency->value() * 1e9;
    const double endHz = std::max(startHz + 1.0, m_endFrequency->value() * 1e9);
    config.centerFrequencyHz = (startHz + endHz) / 2.0;
    config.bandwidthHz = endHz - startHz;
    config.resolutionBandwidthHz = m_resolutionBandwidth->value() * 1e3;
    config.referenceLevelDbm = m_referenceLevel->value();
    config.pointCount = static_cast<std::size_t>(m_pointCount->value());
    config.frameRateHz = m_frameRate->value();
    config.loopFile = m_loopFile->isChecked();
    return config;
}

QString MainWindow::formatFrequency(double hz, int decimals) const
{
    if (std::abs(hz) >= 1e9)
        return QStringLiteral("%1 GHz").arg(hz / 1e9, 0, 'f', decimals);
    return QStringLiteral("%1 MHz").arg(hz / 1e6, 0, 'f', decimals);
}

void MainWindow::browseFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择频谱文件"), QString(),
        QStringLiteral("Spectrum files (*.bin *.dat *.txt *.csv *.asc);;All files (*.*)"));
    if (path.isEmpty()) return;
    m_filePath->setText(path);
    m_sourceCombo->setCurrentIndex(2);
    applyFileMetadata(path);
}

void MainWindow::applyFileMetadata(const QString& path)
{
    source::FileSourceMetadata metadata;
    std::string error;
    if (!source::FileSource::inspectFile(path.toStdString(), metadata, error)) {
        statusBar()->showMessage(QString::fromStdString(error), 6000);
        return;
    }

    if (metadata.hasCenterFrequency && metadata.hasBandwidth) {
        m_startFrequency->setValue((metadata.centerFrequencyHz - metadata.bandwidthHz / 2.0) / 1e9);
        m_endFrequency->setValue((metadata.centerFrequencyHz + metadata.bandwidthHz / 2.0) / 1e9);
    }
    if (metadata.hasResolutionBandwidth) {
        m_resolutionBandwidth->setValue(metadata.resolutionBandwidthHz / 1e3);
    }
    if (metadata.hasReferenceLevel) {
        m_referenceLevel->setValue(metadata.referenceLevelDbm);
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
}

void MainWindow::sourceSelectionChanged(int)
{
    updateSourceControls();
    if (m_sourceCombo && m_statusStrip) m_statusStrip->setDeviceValue(m_sourceCombo->currentText());
}

void MainWindow::updateSourceControls()
{
    if (!m_sourceCombo) return;
    const bool fileSource = m_sourceCombo->currentData().toInt() == static_cast<int>(algorithm::SourceKind::File);
    const bool editable = !m_monitoring;
    m_filePath->setEnabled(fileSource && editable);
    m_browseButton->setEnabled(fileSource && editable);
    m_loopFile->setEnabled(fileSource && editable);
    m_sourceCombo->setEnabled(editable);
    m_startFrequency->setEnabled(editable);
    m_endFrequency->setEnabled(editable);
    m_resolutionBandwidth->setEnabled(editable);
    m_referenceLevel->setEnabled(editable);
    m_pointCount->setEnabled(editable);
    m_frameRate->setEnabled(editable);
    m_applyButton->setEnabled(editable);
}

void MainWindow::applyConfiguration()
{
    if (m_endFrequency->value() <= m_startFrequency->value()) {
        m_endFrequency->setValue(m_startFrequency->value() + 0.001);
    }
    m_controller.configure(currentConfig());
    m_spectrum->resetView();
    m_waterfall->resetView();
    const auto config = currentConfig();
    m_statusStrip->setMenuInfo(config.centerFrequencyHz, config.bandwidthHz,
                               config.resolutionBandwidthHz);
    statusBar()->showMessage(QStringLiteral("参数已提交，等待数据源初始化。"), 3000);
}

void MainWindow::startMonitoring()
{
    if (m_monitoring) {
        stopMonitoring();
        return;
    }
    applyConfiguration();
    m_controller.start();
}

void MainWindow::pauseMonitoring()
{
    if (!m_monitoring) return;
    if (m_stateLabel->text().contains(QStringLiteral("暂停"))) {
        m_controller.resume();
    } else {
        m_controller.pause();
    }
}

void MainWindow::stopMonitoring()
{
    m_controller.stop();
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
    m_pendingSnapshot = snapshot;
}

void MainWindow::refreshDisplay()
{
    const auto snapshot = m_pendingSnapshot;
    if (!snapshot) return;
    m_pendingSnapshot.reset();

    const double endFrequencyHz = snapshot->frame.startFrequencyHz +
        snapshot->frame.binWidthHz * static_cast<double>(snapshot->frame.powerDb.size());
    m_spectrum->setSnapshot(snapshot);
    m_waterfall->setSnapshot(snapshot);
    m_frameLabel->setText(QStringLiteral("帧号：%1").arg(snapshot->frame.sequence));
    m_statusStrip->setMenuInfo((snapshot->frame.startFrequencyHz + endFrequencyHz) / 2.0,
                               endFrequencyHz - snapshot->frame.startFrequencyHz,
                               m_resolutionBandwidth->value() * 1e3);
    updateMonitorMetrics(*snapshot);

    m_signalTable->setRowCount(static_cast<int>(snapshot->detection.detections.size()));
    for (int row = 0; row < m_signalTable->rowCount(); ++row) {
        const auto& signal = snapshot->detection.detections.at(static_cast<std::size_t>(row));
        m_signalTable->setItem(row, 0, tableItem(QString::number(signal.id)));
        m_signalTable->setItem(row, 1, tableItem(QString::number(signal.centerFrequencyHz / 1e6, 'f', 3)));
        m_signalTable->setItem(row, 2, tableItem(QString::number(signal.bandwidthHz / 1e3, 'f', 3)));
        m_signalTable->setItem(row, 3, tableItem(QStringLiteral("接口结果")));
        m_signalTable->setItem(row, 4, tableItem(QStringLiteral("-")));
        m_signalTable->setItem(row, 5, tableItem(QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss"))));
        m_signalTable->setItem(row, 6, tableItem(QStringLiteral("1")));
    }

    if (static_cast<algorithm::SourceKind>(m_sourceCombo->currentData().toInt()) == algorithm::SourceKind::File) {
            m_playbackPage->rememberFile(QString::fromStdString(m_filePath->text().toStdString()),
            m_sourceCombo->currentText(), snapshot->frame.startFrequencyHz,
            endFrequencyHz, m_resolutionBandwidth->value() * 1e3,
            static_cast<int>(snapshot->detection.detections.size()), 0);
    }
}

void MainWindow::updateMonitorMetrics(const algorithm::DisplaySnapshot& snapshot)
{
    int critical = 0;
    int general = 0;
    for (const auto& signal : snapshot.detection.detections) {
        if (signal.confidence >= 0.9F) ++critical;
        else ++general;
    }
    m_criticalAlertLabel->setText(QString::number(critical));
    m_generalAlarmLabel->setText(QString::number(general));
    m_signalTotalLabel->setText(QString::number(snapshot.detection.detections.size()));
}

void MainWindow::onStateChanged(const QString& state)
{
    m_stateLabel->setText(state);
    updateButtonState(state);
    statusBar()->showMessage(state, 4000);
}

void MainWindow::onError(const QString& message)
{
    m_stateLabel->setText(QStringLiteral("错误"));
    statusBar()->showMessage(message, 8000);
    QMessageBox::warning(this, QStringLiteral("监测操作失败"), message);
    updateButtonState(QStringLiteral("Stopped"));
}

void MainWindow::onReplayRequested(const QString& path)
{
    m_filePath->setText(path);
    m_sourceCombo->setCurrentIndex(2);
    applyFileMetadata(path);
    selectMainPage(0);
    statusBar()->showMessage(QStringLiteral("已选择回放文件，可点击开始监测读取。"), 5000);
}

void MainWindow::updateRuntimeStatus()
{
    m_statusStrip->setTime(QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss")));
    m_statusStrip->setLoadValues(0, 0, 0);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
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
