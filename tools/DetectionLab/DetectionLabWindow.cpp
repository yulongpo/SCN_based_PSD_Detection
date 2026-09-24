#include "DetectionLabWindow.h"
#include "../../app/HaiAISpecMonitor/ui/FrequencySpinBox.h"
#include "LabIO.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSplitter>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>

namespace scn::lab
{

namespace
{
QPushButton* actionButton(const QString& text, QWidget* parent,
                          const QString& objectName = QStringLiteral("pageToolButton"))
{
    auto* button = new QPushButton(text, parent);
    button->setObjectName(objectName);
    button->setMinimumHeight(32);
    return button;
}

QTableWidget* resultTable(const QStringList& columns, QWidget* parent)
{
    auto* table = new QTableWidget(0, columns.size(), parent);
    table->setObjectName(QStringLiteral("isaTable"));
    table->setHorizontalHeaderLabels(columns);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    if (columns.size() > 3)
        table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setDefaultSectionSize(30);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors(true);
    table->setShowGrid(true);
    table->setSortingEnabled(false);
    return table;
}

QTableWidgetItem* centeredItem(const QString& text)
{
    auto* item = new QTableWidgetItem(text);
    item->setTextAlignment(Qt::AlignCenter);
    return item;
}

QString boundaryName(algorithm::BoundaryState state)
{
    switch (state) {
    case algorithm::BoundaryState::Stable: return QStringLiteral("稳定");
    case algorithm::BoundaryState::PendingChange: return QStringLiteral("突变待确认");
    case algorithm::BoundaryState::Ambiguous: return QStringLiteral("匹配不唯一");
    default: return QStringLiteral("未启用");
    }
}

QString observationName(algorithm::ObservationState state)
{
    return state == algorithm::ObservationState::Observed
        ? QStringLiteral("已观测") : QStringLiteral("暂未观测");
}

void storeInterval(QTableWidgetItem* item, double start, double end)
{
    item->setData(Qt::UserRole, start);
    item->setData(Qt::UserRole + 1, end);
}

bool parseUnsigned(const QString& text, std::size_t& value)
{
    bool ok = false;
    const auto parsed = text.trimmed().toULongLong(&ok);
    if (!ok || parsed > std::numeric_limits<std::size_t>::max()) return false;
    value = static_cast<std::size_t>(parsed);
    return true;
}
}

DetectionLabWindow::DetectionLabWindow(QWidget* parent)
    : QMainWindow(parent), m_cancelRequested(std::make_shared<std::atomic_bool>(false))
{
    setWindowTitle(QStringLiteral("DetectionLab · 离线频谱检测"));
    setMinimumSize(1160, 760);
    resize(1540, 980);
    buildUi();

    qRegisterMetaType<LabFrameUpdate>("scn::lab::LabFrameUpdate");
    qRegisterMetaType<LabSessionInfo>("scn::lab::LabSessionInfo");
    m_worker = new DetectionLabWorker(m_cancelRequested);
    m_worker->moveToThread(&m_workerThread);
    connect(&m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &DetectionLabWorker::sessionOpened,
            this, &DetectionLabWindow::onSessionOpened);
    connect(m_worker, &DetectionLabWorker::modelInspectionFinished,
            this, &DetectionLabWindow::onModelInspectionFinished);
    connect(m_worker, &DetectionLabWorker::frameReady,
            this, &DetectionLabWindow::onFrameReady);
    connect(m_worker, &DetectionLabWorker::positionChanged,
            this, &DetectionLabWindow::onPositionChanged);
    connect(m_worker, &DetectionLabWorker::stateChanged,
            this, &DetectionLabWindow::onStateChanged);
    connect(m_worker, &DetectionLabWorker::errorOccurred,
            this, &DetectionLabWindow::onError);
    connect(m_worker, &DetectionLabWorker::finished,
            this, &DetectionLabWindow::onFinished);
    m_workerThread.setObjectName(QStringLiteral("DetectionLabWorker"));
    m_workerThread.start();
    updateControls();
}

DetectionLabWindow::~DetectionLabWindow()
{
    m_cancelRequested->store(true, std::memory_order_relaxed);
    if (m_worker) {
        QMetaObject::invokeMethod(m_worker, &DetectionLabWorker::stopSession, Qt::QueuedConnection);
    }
    m_workerThread.quit();
    m_workerThread.wait();
}

void DetectionLabWindow::buildUi()
{
    setStyleSheet(QStringLiteral(R"(
        QMainWindow, QWidget { background: #060610; color: #d6dee8; font-family: "Microsoft YaHei"; }
        QFrame#titlePanel, QFrame#controlPanel, QFrame#displayPanel, QFrame#resultPanel,
        QFrame#statusPanel, QGroupBox { background: #060610; border: 1px solid rgba(255,255,255,40); border-radius: 9px; }
        QGroupBox { margin-top: 10px; padding: 12px 10px 8px 10px; color: #dcecff; font-weight: 600; }
        QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }
        QLabel#pageTitle { color: #e9f2fc; font-size: 22px; font-weight: 700; }
        QLabel#pageHint, QLabel#frameLabel { color: #8294a8; font-size: 12px; }
        QLabel#stateLabel { color: #49b4ff; font-size: 13px; font-weight: 600; }
        QLineEdit, QComboBox, QDoubleSpinBox, QSpinBox { background: #15151e; border: 1px solid #1e1e28; border-radius: 6px; color: #d6dee8; padding: 3px 8px; min-height: 26px; }
        QLineEdit:focus, QComboBox:focus, QDoubleSpinBox:focus, QSpinBox:focus { border: 1px solid #0a8cfe; }
        QPushButton#primaryButton, QPushButton#dangerButton, QPushButton#pageToolButton { border-radius: 6px; min-height: 32px; padding: 0 14px; font-size: 13px; }
        QPushButton#primaryButton { background: #0a8cfe; border: 1px solid #0a8cfe; color: #d6ecff; }
        QPushButton#primaryButton:hover { background: #2c9eff; }
        QPushButton#dangerButton { background: rgba(230,62,62,48); border: 1px solid #8d2e38; color: #e63e3e; }
        QPushButton#dangerButton:hover { background: rgba(230,62,62,72); }
        QPushButton#pageToolButton { background: #15151e; border: 1px solid #1e1e28; color: #c8c8c8; }
        QPushButton#pageToolButton:hover { background: #202d58; border-color: #0a8cfe; }
        QPushButton:disabled { background: #101018; color: #5e6874; border-color: #20232a; }
        QCheckBox { color: #b7c8da; spacing: 8px; }
        QProgressBar { background: #101722; border: 1px solid #263242; border-radius: 5px; height: 16px; text-align: center; color: #d6ecff; }
        QProgressBar::chunk { background: #0a8cfe; border-radius: 4px; }
        QTabWidget::pane { border: 1px solid #1e1e28; border-radius: 8px; top: -1px; }
        QTabBar::tab { background: #11131b; color: #9eafc3; padding: 8px 14px; border: 1px solid #1e1e28; border-bottom: 0; }
        QTabBar::tab:selected { color: #d6ecff; background: #172338; border-top: 2px solid #0a8cfe; }
        QTableWidget#isaTable { background: #060610; alternate-background-color: #15151e; border: 1px solid #1e1e28; border-radius: 8px; gridline-color: #1e1e28; color: #c8c8c8; selection-background-color: #2c3e76; }
        QTableWidget#isaTable QHeaderView::section { background: #15151e; color: #a1a1a5; border: none; border-right: 1px solid #1e1e28; border-bottom: 1px solid #1e1e28; padding: 8px 4px; }
        QTableWidget#isaTable::item { padding: 5px; }
        QPlainTextEdit { background: #080d18; border: 1px solid #202938; color: #b9c9d9; border-radius: 8px; padding: 7px; }
        QSplitter::handle { background: #1e1e28; width: 3px; height: 3px; }
    )"));

    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(14, 12, 14, 10);
    root->setSpacing(10);

    auto* titleRow = new QHBoxLayout;
    auto* title = new QLabel(QStringLiteral("DetectionLab"), central);
    title->setObjectName(QStringLiteral("pageTitle"));
    auto* hint = new QLabel(QStringLiteral("SCN 离线检测、逐帧复现与阶段诊断"), central);
    hint->setObjectName(QStringLiteral("pageHint"));
    titleRow->addWidget(title);
    titleRow->addSpacing(14);
    titleRow->addWidget(hint);
    titleRow->addStretch();
    m_modelStatus = new QLabel(QStringLiteral("模型尚未检查"), central);
    m_modelStatus->setObjectName(QStringLiteral("stateLabel"));
    titleRow->addWidget(m_modelStatus);
    root->addLayout(titleRow);

    auto* controls = new QHBoxLayout;
    m_inspectButton = actionButton(QStringLiteral("检查模型"), central);
    m_inspectButton->setToolTip(QStringLiteral("加载并校验 TensorRT engine 元数据，不执行推理。"));
    m_startButton = actionButton(QStringLiteral("开始检测"), central, QStringLiteral("primaryButton"));
    m_pauseButton = actionButton(QStringLiteral("暂停"), central);
    m_stepButton = actionButton(QStringLiteral("单帧前进"), central);
    m_stopButton = actionButton(QStringLiteral("停止并关闭会话"), central, QStringLiteral("dangerButton"));
    controls->addWidget(m_inspectButton);
    controls->addSpacing(10);
    controls->addWidget(m_startButton);
    controls->addWidget(m_pauseButton);
    controls->addWidget(m_stepButton);
    controls->addWidget(m_stopButton);
    controls->addStretch();
    m_stateLabel = new QLabel(QStringLiteral("就绪"), central);
    m_stateLabel->setObjectName(QStringLiteral("stateLabel"));
    controls->addWidget(m_stateLabel);
    root->addLayout(controls);

    auto* splitter = new QSplitter(Qt::Horizontal, central);
    splitter->setChildrenCollapsible(false);
    m_leftTabs = new QTabWidget(splitter);
    m_leftTabs->setMinimumWidth(360);
    m_leftTabs->setMaximumWidth(520);
    m_leftTabs->addTab(buildInputPage(), QStringLiteral("输入与回放"));
    m_detectionConfig = new app::DetectionConfigWidget(m_leftTabs);
    m_leftTabs->addTab(m_detectionConfig, QStringLiteral("检测参数"));
    if (auto* modelPath = m_detectionConfig->findChild<QLineEdit*>(QStringLiteral("detectionModelPath"))) {
        connect(modelPath, &QLineEdit::textChanged, this, [this] {
            m_modelStatus->setText(QStringLiteral("模型配置已变更"));
            m_modelStatus->setToolTip(QStringLiteral("点击‘检查模型’重新验证当前 TensorRT engine。"));
        });
    }
    m_leftTabs->addTab(buildExportPage(), QStringLiteral("导出与对照"));
    connect(m_detectionConfig, &app::DetectionConfigWidget::applyRequested, this, [this] {
        m_detectionConfig->setFeedback(QStringLiteral("设置已更新；将在下次打开检测会话时校验并应用。"));
    });
    splitter->addWidget(m_leftTabs);

    auto* right = new QSplitter(Qt::Vertical, splitter);
    right->setChildrenCollapsible(false);
    auto* displayPanel = new QFrame(right);
    displayPanel->setObjectName(QStringLiteral("displayPanel"));
    auto* displayLayout = new QVBoxLayout(displayPanel);
    displayLayout->setContentsMargins(10, 8, 10, 8);
    auto* displayHeader = new QHBoxLayout;
    auto* displayTitle = new QLabel(QStringLiteral("频谱与检测标记"), displayPanel);
    displayTitle->setObjectName(QStringLiteral("pageTitle"));
    m_frameLabel = new QLabel(QStringLiteral("等待输入"), displayPanel);
    m_frameLabel->setObjectName(QStringLiteral("frameLabel"));
    displayHeader->addWidget(displayTitle);
    displayHeader->addStretch();
    displayHeader->addWidget(m_frameLabel);
    displayLayout->addLayout(displayHeader);
    m_spectrum = new app::SpectrumWidget(displayPanel);
    m_spectrum->setObjectName(QStringLiteral("spectrumDisplay"));
    displayLayout->addWidget(m_spectrum, 1);
    right->addWidget(displayPanel);

    auto* resultPanel = new QFrame(right);
    resultPanel->setObjectName(QStringLiteral("resultPanel"));
    auto* resultLayout = new QVBoxLayout(resultPanel);
    resultLayout->setContentsMargins(8, 6, 8, 8);
    auto* resultHeader = new QHBoxLayout;
    auto* resultTitle = new QLabel(QStringLiteral("本帧结果"), resultPanel);
    resultTitle->setObjectName(QStringLiteral("pageTitle"));
    m_statisticsLabel = new QLabel(QStringLiteral("等待检测"), resultPanel);
    m_statisticsLabel->setObjectName(QStringLiteral("pageHint"));
    resultHeader->addWidget(resultTitle);
    resultHeader->addStretch();
    resultHeader->addWidget(m_statisticsLabel);
    resultLayout->addLayout(resultHeader);
    m_resultTabs = new QTabWidget(resultPanel);
    m_rawTable = resultTable({QStringLiteral("ID"), QStringLiteral("起始频率"), QStringLiteral("终止频率"),
        QStringLiteral("中心频率"), QStringLiteral("带宽"), QStringLiteral("置信度"),
        QStringLiteral("信号电平"), QStringLiteral("噪底"), QStringLiteral("CNR")}, m_resultTabs);
    m_trackedTable = resultTable({QStringLiteral("ID"), QStringLiteral("稳定起点"), QStringLiteral("稳定终点"),
        QStringLiteral("稳定带宽"), QStringLiteral("边界状态"), QStringLiteral("确认进度"),
        QStringLiteral("关联 IoU"), QStringLiteral("中心距离"), QStringLiteral("带宽比")}, m_resultTabs);
    m_channelTable = resultTable({QStringLiteral("ID"), QStringLiteral("类型"), QStringLiteral("起始频率"),
        QStringLiteral("终止频率"), QStringLiteral("带宽"), QStringLiteral("覆盖率"),
        QStringLiteral("状态"), QStringLiteral("先验"), QStringLiteral("诊断")}, m_resultTabs);
    m_groupingTable = resultTable({QStringLiteral("候选索引"), QStringLiteral("起始频率"),
        QStringLiteral("终止频率"), QStringLiteral("历史覆盖"), QStringLiteral("已知比例"),
        QStringLiteral("当前占用"), QStringLiteral("信道 ID"), QStringLiteral("处理结果")}, m_resultTabs);
    m_resultTabs->addTab(m_rawTable, QStringLiteral("原始检测"));
    m_resultTabs->addTab(m_trackedTable, QStringLiteral("稳定跟踪"));
    m_resultTabs->addTab(m_channelTable, QStringLiteral("信道聚合"));
    m_resultTabs->addTab(m_groupingTable, QStringLiteral("聚合诊断"));
    resultLayout->addWidget(m_resultTabs, 1);
    right->addWidget(resultPanel);
    right->setStretchFactor(0, 3);
    right->setStretchFactor(1, 2);
    splitter->addWidget(right);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    root->addWidget(splitter, 1);

    auto* footer = new QHBoxLayout;
    m_progress = new QProgressBar(central);
    m_progress->setRange(0, 1);
    m_progress->setValue(0);
    m_progress->setFormat(QStringLiteral("未开始"));
    footer->addWidget(m_progress, 1);
    m_seekFrame = new QLineEdit(QStringLiteral("0"), central);
    m_seekFrame->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("\\d{0,20}")), m_seekFrame));
    m_seekFrame->setMaximumWidth(130);
    m_seekButton = actionButton(QStringLiteral("跳转到帧"), central);
    footer->addWidget(new QLabel(QStringLiteral("帧号"), central));
    footer->addWidget(m_seekFrame);
    footer->addWidget(m_seekButton);
    root->addLayout(footer);

    m_log = new QPlainTextEdit(central);
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(500);
    m_log->setMaximumHeight(92);
    root->addWidget(m_log);

    setCentralWidget(central);
    connect(m_inspectButton, &QPushButton::clicked, this, &DetectionLabWindow::inspectModel);
    connect(m_startButton, &QPushButton::clicked, this, &DetectionLabWindow::startRun);
    connect(m_pauseButton, &QPushButton::clicked, this, &DetectionLabWindow::pauseRun);
    connect(m_stepButton, &QPushButton::clicked, this, &DetectionLabWindow::stepOnce);
    connect(m_stopButton, &QPushButton::clicked, this, &DetectionLabWindow::stopSession);
    connect(m_seekButton, &QPushButton::clicked, this, &DetectionLabWindow::seekToFrame);
    connect(m_referenceFile, &QLineEdit::textChanged, this, [this] { updateControls(); });
    for (auto* table : {m_rawTable, m_trackedTable, m_channelTable}) {
        connect(table, &QTableWidget::itemSelectionChanged, this, [this, table] { zoomToRow(table); });
    }
}

QWidget* DetectionLabWindow::buildInputPage()
{
    auto* page = new QWidget(m_leftTabs);
    auto* root = new QVBoxLayout(page);
    root->setContentsMargins(8, 8, 8, 8);
    auto* form = new QFormLayout;
    form->setVerticalSpacing(9);
    m_inputFile = makePathRow(form, QStringLiteral("频谱文件"), QStringLiteral("浏览"),
        QStringLiteral("labInputFile"), [this] { browseInput(); });
    m_configFile = makePathRow(form, QStringLiteral("配置 JSON"), QStringLiteral("导入"),
        QStringLiteral("labConfigFile"), [this] { browseConfig(); });
    root->addLayout(form);

    auto* metadataGroup = new QGroupBox(QStringLiteral("文件元数据与回退值"), page);
    auto* metadataForm = new QFormLayout(metadataGroup);
    m_inputMetadata = new QLabel(QStringLiteral("选择文件后读取频率元数据。文件名包含 Fc/Bw/Rbw/Reflevel/SpectrumLen 时优先使用。"), metadataGroup);
    m_inputMetadata->setWordWrap(true);
    m_inputMetadata->setObjectName(QStringLiteral("pageHint"));
    metadataForm->addRow(m_inputMetadata);
    m_pointCount = new QSpinBox(metadataGroup);
    m_pointCount->setRange(1, 20'000'000);
    m_pointCount->setValue(202242);
    metadataForm->addRow(QStringLiteral("点数"), m_pointCount);
    m_centerFrequency = new app::FrequencySpinBox(metadataGroup);
    m_centerFrequency->setRange(0.0, 6.4e9);
    m_centerFrequency->setFrequencyHz(2025000000LL);
    metadataForm->addRow(QStringLiteral("中心频率"), m_centerFrequency);
    m_span = new app::FrequencySpinBox(metadataGroup);
    m_span->setRange(1.0, 6.4e9);
    m_span->setFrequencyHz(3950000000LL);
    metadataForm->addRow(QStringLiteral("频谱跨度"), m_span);
    m_rbw = new app::FrequencySpinBox(metadataGroup);
    m_rbw->setRange(1.0, 6.4e9);
    m_rbw->setFrequencyHz(50000LL);
    metadataForm->addRow(QStringLiteral("RBW"), m_rbw);
    m_referenceLevel = new QDoubleSpinBox(metadataGroup);
    m_referenceLevel->setRange(-200.0, 100.0);
    m_referenceLevel->setDecimals(2);
    m_referenceLevel->setValue(-20.0);
    m_referenceLevel->setSuffix(QStringLiteral(" dBm"));
    metadataForm->addRow(QStringLiteral("参考电平"), m_referenceLevel);
    root->addWidget(metadataGroup);

    auto* replayGroup = new QGroupBox(QStringLiteral("回放范围"), page);
    auto* replayForm = new QFormLayout(replayGroup);
    const auto unsignedLine = [replayGroup](const QString& initial) {
        auto* edit = new QLineEdit(initial, replayGroup);
        edit->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("\\d{0,20}")), edit));
        return edit;
    };
    m_startFrame = unsignedLine(QStringLiteral("0"));
    replayForm->addRow(QStringLiteral("起始帧（从 0 开始）"), m_startFrame);
    m_maximumFrames = unsignedLine(QStringLiteral("0"));
    replayForm->addRow(QStringLiteral("最大帧数（0=全部）"), m_maximumFrames);
    m_logicalFps = new QSpinBox(replayGroup);
    m_logicalFps->setRange(1, 1'000'000);
    m_logicalFps->setValue(30);
    replayForm->addRow(QStringLiteral("逻辑帧率（Hz）"), m_logicalFps);
    root->addWidget(replayGroup);
    root->addStretch();
    connect(m_inputFile, &QLineEdit::editingFinished,
            this, &DetectionLabWindow::refreshInputMetadata);
    return page;
}

QWidget* DetectionLabWindow::buildExportPage()
{
    auto* page = new QWidget(m_leftTabs);
    auto* root = new QVBoxLayout(page);
    root->setContentsMargins(8, 8, 8, 8);
    auto* form = new QFormLayout;
    form->setVerticalSpacing(9);
    m_outputFile = makePathRow(form, QStringLiteral("逐帧 JSONL"), QStringLiteral("选择"),
        QStringLiteral("labOutputFile"), [this] {
            const auto path = QFileDialog::getSaveFileName(this, QStringLiteral("选择 JSONL 新文件"),
                m_outputFile->text(), QStringLiteral("JSON Lines (*.jsonl);;All files (*)"));
            if (!path.isEmpty()) m_outputFile->setText(path);
        });
    m_csvFile = makePathRow(form, QStringLiteral("信号 CSV"), QStringLiteral("选择"),
        QStringLiteral("labCsvFile"), [this] {
            const auto path = QFileDialog::getSaveFileName(this, QStringLiteral("选择 CSV 新文件"),
                m_csvFile->text(), QStringLiteral("CSV (*.csv);;All files (*)"));
            if (!path.isEmpty()) m_csvFile->setText(path);
        });
    m_dumpDirectory = makePathRow(form, QStringLiteral("阶段数据目录"), QStringLiteral("选择"),
        QStringLiteral("labDumpDirectory"), [this] {
            const auto path = QFileDialog::getExistingDirectory(this, QStringLiteral("选择阶段数据目录"),
                m_dumpDirectory->text());
            if (!path.isEmpty()) m_dumpDirectory->setText(path);
        });
    m_referenceFile = makePathRow(form, QStringLiteral("参考 JSONL"), QStringLiteral("选择"),
        QStringLiteral("labReferenceFile"), [this] {
            const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("选择参考 JSONL"),
                m_referenceFile->text(), QStringLiteral("JSON Lines (*.jsonl);;All files (*)"));
            if (!path.isEmpty()) m_referenceFile->setText(path);
        });
    root->addLayout(form);
    auto* exportConfigButton = actionButton(QStringLiteral("导出检测配置 JSON"), page);
    root->addWidget(exportConfigButton, 0, Qt::AlignLeft);
    connect(exportConfigButton, &QPushButton::clicked, this, &DetectionLabWindow::exportConfig);
    auto* hint = new QLabel(QStringLiteral(
        "所有检测输出都创建新文件，拒绝覆盖输入、模型、配置或已有输出。阶段导出会写入独立 run-UUID 子目录。\n"
        "参考对照要求顺序处理；启用对照时不可跳帧。"), page);
    hint->setWordWrap(true);
    hint->setObjectName(QStringLiteral("pageHint"));
    root->addWidget(hint);
    root->addStretch();
    return page;
}

QLineEdit* DetectionLabWindow::makePathRow(QFormLayout* form, const QString& label,
                                            const QString& buttonText, const QString& objectName,
                                            const std::function<void()>& browse)
{
    auto* row = new QWidget(form->parentWidget());
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* edit = new QLineEdit(row);
    edit->setObjectName(objectName);
    auto* button = actionButton(buttonText, row);
    layout->addWidget(edit, 1);
    layout->addWidget(button);
    form->addRow(label, row);
    connect(button, &QPushButton::clicked, this, [browse] { browse(); });
    return edit;
}

void DetectionLabWindow::browseInput()
{
    const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("选择频谱文件"),
        m_inputFile->text(), QStringLiteral("Spectrum files (*.dat *.bin *.txt *.csv *.asc);;All files (*)"));
    if (path.isEmpty()) return;
    stopSession();
    m_inputFile->setText(path);
    refreshInputMetadata();
}

void DetectionLabWindow::browseConfig()
{
    const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("导入检测配置"),
        m_configFile->text(), QStringLiteral("JSON (*.json);;All files (*)"));
    if (path.isEmpty()) return;
    m_configFile->setText(path);
    importConfig();
}

void DetectionLabWindow::importConfig()
{
    if (m_configFile->text().trimmed().isEmpty()) {
        browseConfig();
        return;
    }
    try {
        const auto config = readConfiguration(m_configFile->text().trimmed());
        m_detectionConfig->setConfig(config);
        m_detectionConfig->setFeedback(QStringLiteral("已导入配置 JSON。"));
        appendLog(QStringLiteral("已导入配置：%1").arg(m_configFile->text()));
    } catch (const std::exception& error) {
        showError(QString::fromUtf8(error.what()));
    }
}

void DetectionLabWindow::exportConfig()
{
    const auto path = QFileDialog::getSaveFileName(this, QStringLiteral("导出检测配置（新文件）"),
        QString(), QStringLiteral("JSON (*.json);;All files (*)"));
    if (path.isEmpty()) return;
    try {
        writeJsonFile(path, configurationJson(m_detectionConfig->config()));
        appendLog(QStringLiteral("配置已写入：%1").arg(path));
    } catch (const std::exception& error) {
        showError(QString::fromUtf8(error.what()));
    }
}

void DetectionLabWindow::inspectModel()
{
    auto config = m_detectionConfig->config();
    config.enabled = true;
    m_inspectButton->setEnabled(false);
    m_modelStatus->setText(QStringLiteral("正在检查模型…"));
    appendLog(QStringLiteral("开始加载模型并检查元数据（不执行推理）。"));
    QMetaObject::invokeMethod(m_worker, [worker = m_worker, config = std::move(config)]() mutable {
        worker->inspectModel(std::move(config));
    }, Qt::QueuedConnection);
}

void DetectionLabWindow::startRun()
{
    if (m_sessionOpened) {
        QMetaObject::invokeMethod(m_worker, &DetectionLabWorker::startRun, Qt::QueuedConnection);
        return;
    }
    if (m_openPending) return;
    m_runAfterOpen = true;
    m_stepAfterOpen = false;
    beginSession();
}

void DetectionLabWindow::pauseRun()
{
    QMetaObject::invokeMethod(m_worker, &DetectionLabWorker::pauseRun, Qt::QueuedConnection);
}

void DetectionLabWindow::stepOnce()
{
    if (m_referenceFile && !m_referenceFile->text().trimmed().isEmpty()) {
        showError(QStringLiteral("参考对照模式要求顺序处理，已禁用单帧手动操作。"));
        return;
    }
    if (m_sessionOpened) {
        QMetaObject::invokeMethod(m_worker, &DetectionLabWorker::stepOnce, Qt::QueuedConnection);
        return;
    }
    m_runAfterOpen = false;
    m_stepAfterOpen = true;
    m_openPending = true;
    beginSession();
}

void DetectionLabWindow::seekToFrame()
{
    if (!m_referenceFile->text().trimmed().isEmpty()) {
        showError(QStringLiteral("参考对照模式不支持跳帧。"));
        return;
    }
    std::size_t frameIndex = 0;
    if (!parseUnsigned(m_seekFrame->text(), frameIndex)) {
        showError(QStringLiteral("帧号必须是非负整数。"));
        return;
    }
    if (!m_sessionOpened) {
        m_startFrame->setText(QString::number(static_cast<qulonglong>(frameIndex)));
        m_runAfterOpen = false;
        m_stepAfterOpen = false;
        beginSession();
        return;
    }
    QMetaObject::invokeMethod(m_worker, [worker = m_worker, frameIndex] {
        worker->seekTo(static_cast<quint64>(frameIndex));
    }, Qt::QueuedConnection);
}

void DetectionLabWindow::stopSession()
{
    m_cancelRequested->store(true, std::memory_order_relaxed);
    if (m_worker)
        QMetaObject::invokeMethod(m_worker, &DetectionLabWorker::stopSession, Qt::QueuedConnection);
    m_sessionOpened = false;
    m_openPending = false;
    m_runAfterOpen = false;
    m_stepAfterOpen = false;
    m_isRunning = false;
    m_stopPending = true;
    m_progress->setValue(0);
    m_progress->setFormat(QStringLiteral("已停止"));
    updateControls();
}

void DetectionLabWindow::beginSession()
{
    refreshInputMetadata();
    LabRunOptions options;
    QString error;
    if (!collectOptions(options, error)) {
        m_runAfterOpen = false;
        m_stepAfterOpen = false;
        showError(error);
        return;
    }
    m_openPending = true;
    m_cancelRequested->store(false, std::memory_order_relaxed);
    m_stateLabel->setText(QStringLiteral("正在准备检测会话…"));
    appendLog(QStringLiteral("打开输入文件并初始化检测引擎。"));
    updateControls();
    QMetaObject::invokeMethod(m_worker, [worker = m_worker, options = std::move(options)]() mutable {
        worker->openSession(std::move(options));
    }, Qt::QueuedConnection);
}

bool DetectionLabWindow::collectOptions(LabRunOptions& options, QString& error) const
{
    options.config = m_detectionConfig->config();
    options.inputFile = m_inputFile->text().trimmed();
    options.configFile = m_configFile->text().trimmed();
    options.outputFile = m_outputFile->text().trimmed();
    options.csvFile = m_csvFile->text().trimmed();
    options.dumpDirectory = m_dumpDirectory->text().trimmed();
    options.referenceFile = m_referenceFile->text().trimmed();
    options.logicalFrameRateHz = static_cast<std::uint32_t>(m_logicalFps->value());
    options.fallbackPointCount = static_cast<std::size_t>(m_pointCount->value());
    options.fallbackCenterFrequencyHz = m_centerFrequency->frequencyHz();
    options.fallbackSpanHz = m_span->frequencyHz();
    options.fallbackResolutionBandwidthHz = m_rbw->frequencyHz();
    options.fallbackReferenceLevelDbm = m_referenceLevel->value();
    options.interactiveStep = options.referenceFile.isEmpty();
    if (!parseUnsigned(m_startFrame->text(), options.startFrame) ||
        !parseUnsigned(m_maximumFrames->text(), options.maximumFrames)) {
        error = QStringLiteral("起始帧和最大帧数必须是非负整数。");
        return false;
    }
    if (options.inputFile.isEmpty()) {
        error = QStringLiteral("请选择频谱输入文件。");
        return false;
    }
    std::string validationError;
    if (!algorithm::validateConfig(options.config, validationError)) {
        error = QString::fromStdString(validationError);
        return false;
    }
    error.clear();
    return true;
}

void DetectionLabWindow::onSessionOpened(const LabSessionInfo& info)
{
    m_sessionOpened = true;
    m_openPending = false;
    m_frameCount = info.frameCount;
    m_totalFrames = info.plannedFrames;
    m_processedFrames = 0;
    m_modelStatus->setText(QStringLiteral("模型已就绪"));
    m_modelStatus->setToolTip(info.modelInfo);
    m_progress->setRange(0, static_cast<int>(std::min<std::size_t>(info.plannedFrames,
        static_cast<std::size_t>(std::numeric_limits<int>::max()))));
    m_progress->setValue(0);
    m_progress->setFormat(QStringLiteral("0 / %1").arg(static_cast<qulonglong>(info.plannedFrames)));
    m_seekFrame->setText(QString::number(static_cast<qulonglong>(info.startFrame)));
    m_spectrum->setDisplayDomain(info.centerFrequencyHz - info.spanHz / 2.0,
                                 info.centerFrequencyHz + info.spanHz / 2.0,
                                 info.referenceLevelDbm);
    m_hasCenterMetadata = info.metadata.hasCenterFrequency;
    m_hasSpanMetadata = info.metadata.hasBandwidth;
    m_hasRbwMetadata = info.metadata.hasResolutionBandwidth;
    m_hasReferenceMetadata = info.metadata.hasReferenceLevel;
    m_hasPointMetadata = info.metadata.hasSpectrumLength;
    updateMetadataHint();
    appendLog(QStringLiteral("输入已打开：%1 帧；有效处理上限 %2 帧。")
        .arg(static_cast<qulonglong>(info.frameCount)).arg(static_cast<qulonglong>(info.plannedFrames)));
    if (!info.stageDirectory.isEmpty())
        appendLog(QStringLiteral("阶段数据目录：%1").arg(info.stageDirectory));
    updateControls();
    if (m_runAfterOpen) {
        m_runAfterOpen = false;
        QMetaObject::invokeMethod(m_worker, &DetectionLabWorker::startRun, Qt::QueuedConnection);
    } else if (m_stepAfterOpen) {
        m_stepAfterOpen = false;
        QMetaObject::invokeMethod(m_worker, &DetectionLabWorker::stepOnce, Qt::QueuedConnection);
    }
}

void DetectionLabWindow::onModelInspectionFinished(bool success, const QString& modelInfo,
                                                    const QString& error)
{
    m_inspectButton->setEnabled(true);
    if (success) {
        m_modelStatus->setText(QStringLiteral("模型检查通过"));
        m_modelStatus->setToolTip(modelInfo);
        appendLog(QStringLiteral("模型检查通过：%1").arg(modelInfo));
    } else {
        m_modelStatus->setText(QStringLiteral("模型检查失败"));
        m_modelStatus->setToolTip(error);
        showError(error);
    }
    updateControls();
}

void DetectionLabWindow::onFrameReady(const LabFrameUpdate& update)
{
    applyFrame(update);
}

void DetectionLabWindow::applyFrame(const LabFrameUpdate& update)
{
    m_processedFrames = update.processedFrames;
    m_spectrum->setSnapshot(update.snapshot);
    auto policy = std::make_shared<application::policy::PolicySnapshot>();
    policy->generation = update.detection.generation;
    policy->detectionConfigVersion = update.detection.configVersion;
    policy->sequence = update.frame.sequence;
    const auto appendPolicySignal = [&](std::int64_t id, const algorithm::DetectedSignal& raw,
                                        const algorithm::DetectedSignal& stable,
                                        algorithm::BoundaryState boundary,
                                        algorithm::ObservationState observation,
                                        bool aggregate, const QString& displayId) {
        application::policy::PolicySignal item;
        item.source = application::policy::PolicySignalSource::RawDetection;
        item.id = id;
        item.displayId = displayId.toStdString();
        item.measurement = stable;
        item.rawMeasurement = raw;
        item.stableMeasurement = stable;
        item.boundaryState = boundary;
        item.observationState = observation;
        item.aggregate = aggregate;
        item.hasBoundaryMetadata = true;
        policy->businessSignals.push_back(std::move(item));
    };
    if (!update.detection.channelDetections.empty()) {
        for (const auto& channel : update.detection.channelDetections) {
            appendPolicySignal(channel.stable.id, channel.raw, channel.stable, channel.boundaryState,
                channel.observationState, channel.aggregate,
                QStringLiteral("%1%2").arg(channel.aggregate ? QStringLiteral("CH") : QStringLiteral("S"))
                    .arg(channel.stable.id));
        }
    } else if (!update.detection.trackedDetections.empty()) {
        for (const auto& tracked : update.detection.trackedDetections) {
            appendPolicySignal(tracked.stable.id, tracked.raw, tracked.stable,
                tracked.boundaryState, algorithm::ObservationState::Observed, false,
                QStringLiteral("S%1").arg(tracked.stable.id));
        }
    } else {
        for (const auto& signal : update.detection.detections) {
            appendPolicySignal(signal.id, signal, signal, algorithm::BoundaryState::Disabled,
                algorithm::ObservationState::Observed, false,
                QStringLiteral("S%1").arg(signal.id));
        }
    }
    m_spectrum->setPolicySnapshot(policy);
    updateDetectionTables(update.detection);

    const int maximum = static_cast<int>(std::min<std::size_t>(m_totalFrames,
        static_cast<std::size_t>(std::numeric_limits<int>::max())));
    m_progress->setRange(0, std::max(1, maximum));
    m_progress->setValue(static_cast<int>(std::min<std::size_t>(m_processedFrames,
        static_cast<std::size_t>(std::numeric_limits<int>::max()))));
    m_progress->setFormat(QStringLiteral("%1 / %2 帧")
        .arg(static_cast<qulonglong>(m_processedFrames)).arg(static_cast<qulonglong>(m_totalFrames)));
    m_frameLabel->setText(QStringLiteral("文件帧 %1 / %2 · 累积 %3/%4 · 检测 %5 · 聚合信道 %6 · %7 窗 · %8 ms")
        .arg(static_cast<qulonglong>(update.fileFrameIndex))
        .arg(static_cast<qulonglong>(m_frameCount))
        .arg(static_cast<qulonglong>(update.detection.accumulatedFrames))
        .arg(static_cast<qulonglong>(update.detection.requiredFrames))
        .arg(static_cast<qulonglong>(update.detection.detections.size()))
        .arg(static_cast<qulonglong>(update.detection.channelDetections.size()))
        .arg(static_cast<qulonglong>(update.detection.diagnostics.windowCount))
        .arg(update.detection.diagnostics.processingTimeMs, 0, 'f', 2));
    m_statisticsLabel->setText(QStringLiteral("单帧 %1 ms · 均值 %2 ms · P50/P95 %3/%4 ms · %5 帧/秒 · %6 秒")
        .arg(update.detection.diagnostics.processingTimeMs, 0, 'f', 2)
        .arg(update.statistics.averageMs, 0, 'f', 2)
        .arg(update.statistics.p50Ms, 0, 'f', 2)
        .arg(update.statistics.p95Ms, 0, 'f', 2)
        .arg(update.statistics.throughputHz, 0, 'f', 2)
        .arg(update.statistics.elapsedSeconds, 0, 'f', 1));
}

void DetectionLabWindow::updateDetectionTables(const algorithm::DetectionResult& result)
{
    m_rawTable->setRowCount(static_cast<int>(result.detections.size()));
    for (int row = 0; row < m_rawTable->rowCount(); ++row) {
        const auto& signal = result.detections[static_cast<std::size_t>(row)];
        auto* id = centeredItem(QString::number(signal.id));
        storeInterval(id, signal.startFrequencyHz, signal.endFrequencyHz);
        m_rawTable->setItem(row, 0, id);
        m_rawTable->setItem(row, 1, centeredItem(formatFrequency(signal.startFrequencyHz)));
        m_rawTable->setItem(row, 2, centeredItem(formatFrequency(signal.endFrequencyHz)));
        m_rawTable->setItem(row, 3, centeredItem(formatFrequency(signal.centerFrequencyHz)));
        m_rawTable->setItem(row, 4, centeredItem(formatFrequency(signal.bandwidthHz)));
        m_rawTable->setItem(row, 5, centeredItem(QString::number(signal.confidence, 'f', 3)));
        m_rawTable->setItem(row, 6, centeredItem(QString::number(signal.signalLevelDbm, 'f', 2)));
        m_rawTable->setItem(row, 7, centeredItem(QString::number(signal.noiseLevelDbm, 'f', 2)));
        m_rawTable->setItem(row, 8, centeredItem(QString::number(signal.snrDb, 'f', 2)));
    }

    m_trackedTable->setRowCount(static_cast<int>(result.trackedDetections.size()));
    for (int row = 0; row < m_trackedTable->rowCount(); ++row) {
        const auto& tracked = result.trackedDetections[static_cast<std::size_t>(row)];
        auto* id = centeredItem(QString::number(tracked.stable.id));
        storeInterval(id, tracked.stable.startFrequencyHz, tracked.stable.endFrequencyHz);
        m_trackedTable->setItem(row, 0, id);
        m_trackedTable->setItem(row, 1, centeredItem(formatFrequency(tracked.stable.startFrequencyHz)));
        m_trackedTable->setItem(row, 2, centeredItem(formatFrequency(tracked.stable.endFrequencyHz)));
        m_trackedTable->setItem(row, 3, centeredItem(formatFrequency(tracked.stable.bandwidthHz)));
        m_trackedTable->setItem(row, 4, centeredItem(boundaryName(tracked.boundaryState)));
        m_trackedTable->setItem(row, 5, centeredItem(QStringLiteral("%1 / %2")
            .arg(static_cast<qulonglong>(tracked.pendingCount)).arg(static_cast<qulonglong>(tracked.requiredCount))));
        m_trackedTable->setItem(row, 6, centeredItem(QString::number(tracked.associationIou, 'f', 3)));
        m_trackedTable->setItem(row, 7, centeredItem(formatFrequency(tracked.centerDistanceHz)));
        m_trackedTable->setItem(row, 8, centeredItem(QString::number(tracked.bandwidthRatio, 'f', 3)));
    }

    m_channelTable->setRowCount(static_cast<int>(result.channelDetections.size()));
    for (int row = 0; row < m_channelTable->rowCount(); ++row) {
        const auto& channel = result.channelDetections[static_cast<std::size_t>(row)];
        auto* id = centeredItem(QString::number(channel.stable.id));
        storeInterval(id, channel.stable.startFrequencyHz, channel.stable.endFrequencyHz);
        m_channelTable->setItem(row, 0, id);
        m_channelTable->setItem(row, 1, centeredItem(channel.aggregate ? QStringLiteral("聚合信道") : QStringLiteral("单体信号")));
        m_channelTable->setItem(row, 2, centeredItem(formatFrequency(channel.stable.startFrequencyHz)));
        m_channelTable->setItem(row, 3, centeredItem(formatFrequency(channel.stable.endFrequencyHz)));
        m_channelTable->setItem(row, 4, centeredItem(formatFrequency(channel.stable.bandwidthHz)));
        m_channelTable->setItem(row, 5, centeredItem(QStringLiteral("%1%").arg(channel.occupancyCoverage * 100.0, 0, 'f', 1)));
        m_channelTable->setItem(row, 6, centeredItem(observationName(channel.observationState)));
        m_channelTable->setItem(row, 7, centeredItem(QString::fromStdString(channel.priorName)));
        m_channelTable->setItem(row, 8, centeredItem(QString::fromStdString(channel.diagnostic)));
    }

    m_groupingTable->setRowCount(static_cast<int>(result.channelGroupingDiagnostics.size()));
    for (int row = 0; row < m_groupingTable->rowCount(); ++row) {
        const auto& diagnostic = result.channelGroupingDiagnostics[static_cast<std::size_t>(row)];
        QStringList indexes;
        for (const auto index : diagnostic.candidateIndices) indexes.push_back(QString::number(index));
        auto* ids = centeredItem(indexes.join(QStringLiteral(", ")));
        storeInterval(ids, diagnostic.startFrequencyHz, diagnostic.endFrequencyHz);
        m_groupingTable->setItem(row, 0, ids);
        m_groupingTable->setItem(row, 1, centeredItem(formatFrequency(diagnostic.startFrequencyHz)));
        m_groupingTable->setItem(row, 2, centeredItem(formatFrequency(diagnostic.endFrequencyHz)));
        m_groupingTable->setItem(row, 3, centeredItem(QStringLiteral("%1%").arg(diagnostic.occupancyCoverage * 100.0, 0, 'f', 1)));
        m_groupingTable->setItem(row, 4, centeredItem(QStringLiteral("%1%").arg(diagnostic.currentKnownRatio * 100.0, 0, 'f', 1)));
        m_groupingTable->setItem(row, 5, centeredItem(QStringLiteral("%1%").arg(diagnostic.currentOccupiedRatio * 100.0, 0, 'f', 1)));
        m_groupingTable->setItem(row, 6, centeredItem(QString::number(diagnostic.resultingChannelId)));
        m_groupingTable->setItem(row, 7, centeredItem(QString::fromStdString(diagnostic.disposition)));
    }
}

void DetectionLabWindow::onPositionChanged(quint64 frameIndex)
{
    m_seekFrame->setText(QString::number(frameIndex));
}

void DetectionLabWindow::onStateChanged(const QString& state)
{
    m_stateLabel->setText(state);
    m_isRunning = state == QStringLiteral("正在处理") || state == QStringLiteral("单帧处理");
    if (state == QStringLiteral("已停止")) {
        m_sessionOpened = false;
        m_openPending = false;
        m_stopPending = false;
    }
    updateControls();
}

void DetectionLabWindow::onError(const QString& error)
{
    m_openPending = false;
    m_runAfterOpen = false;
    m_stepAfterOpen = false;
    appendLog(QStringLiteral("错误：%1").arg(error));
    showError(error);
    updateControls();
}

void DetectionLabWindow::onFinished()
{
    m_isRunning = false;
    appendLog(QStringLiteral("文件处理到达结尾或帧数上限。"));
    updateControls();
}

void DetectionLabWindow::refreshInputMetadata()
{
    if (!m_inputFile) return;
    const auto fileName = m_inputFile->text().trimmed();
    if (fileName.isEmpty()) {
        m_hasCenterMetadata = false;
        m_hasSpanMetadata = false;
        m_hasRbwMetadata = false;
        m_hasReferenceMetadata = false;
        m_hasPointMetadata = false;
        m_inputMetadata->setText(QStringLiteral("选择文件后读取频率元数据。"));
        return;
    }
    source::FileSourceMetadata metadata;
    std::string error;
    if (!source::FileSource::inspectFile(fileName.toStdString(), metadata, error)) {
        m_hasCenterMetadata = false;
        m_hasSpanMetadata = false;
        m_hasRbwMetadata = false;
        m_hasReferenceMetadata = false;
        m_hasPointMetadata = false;
        m_inputMetadata->setText(QString::fromStdString(error));
        return;
    }
    m_hasCenterMetadata = metadata.hasCenterFrequency;
    m_hasSpanMetadata = metadata.hasBandwidth;
    m_hasRbwMetadata = metadata.hasResolutionBandwidth;
    m_hasReferenceMetadata = metadata.hasReferenceLevel;
    m_hasPointMetadata = metadata.hasSpectrumLength;
    std::size_t displayFrames = metadata.completeFrameCount;
    if (!metadata.hasSpectrumLength && m_pointCount->value() > 0)
        displayFrames = metadata.isBinary
            ? static_cast<std::size_t>(metadata.fileSizeBytes / (static_cast<std::uint64_t>(m_pointCount->value()) * sizeof(float)))
            : 0;
    const QString summary = QStringLiteral("%1 · %2 · %3 字节")
        .arg(metadata.isBinary ? QStringLiteral("float32 二进制") : QStringLiteral("文本频谱"))
        .arg(displayFrames ? QStringLiteral("%1 帧").arg(static_cast<qulonglong>(displayFrames))
                           : QStringLiteral("帧数打开后确认"))
        .arg(static_cast<qulonglong>(metadata.fileSizeBytes));
    updateMetadataHint();
    const QStringList sourceFields{
        m_hasCenterMetadata ? QStringLiteral("Fc 文件名") : QStringLiteral("Fc 回退"),
        m_hasSpanMetadata ? QStringLiteral("Bw 文件名") : QStringLiteral("Bw 回退"),
        m_hasRbwMetadata ? QStringLiteral("Rbw 文件名") : QStringLiteral("Rbw 回退"),
        m_hasReferenceMetadata ? QStringLiteral("Reflevel 文件名") : QStringLiteral("Reflevel 回退"),
        m_hasPointMetadata ? QStringLiteral("SpectrumLen 文件名") : QStringLiteral("点数回退")};
    const QString effectiveValues = QStringLiteral("Fc %1 · Bw %2 · RBW %3 · Ref %4 dBm · %5 点")
        .arg(metadata.hasCenterFrequency ? formatFrequency(metadata.centerFrequencyHz) : formatFrequency(m_centerFrequency->frequencyHz()))
        .arg(metadata.hasBandwidth ? formatFrequency(metadata.bandwidthHz) : formatFrequency(m_span->frequencyHz()))
        .arg(metadata.hasResolutionBandwidth ? formatFrequency(metadata.resolutionBandwidthHz) : formatFrequency(m_rbw->frequencyHz()))
        .arg(metadata.hasReferenceLevel ? metadata.referenceLevelDbm : m_referenceLevel->value(), 0, 'f', 2)
        .arg(static_cast<qulonglong>(metadata.hasSpectrumLength ? metadata.spectrumLength : static_cast<std::size_t>(m_pointCount->value())));
    m_inputMetadata->setText(summary + QStringLiteral("\n当前来源：") + sourceFields.join(QStringLiteral("，"))
        + QStringLiteral("\n有效值：") + effectiveValues);
    updateControls();
}

void DetectionLabWindow::updateMetadataHint()
{
    if (!m_inputMetadata) return;
    const QStringList sourceFields{
        m_hasCenterMetadata ? QStringLiteral("Fc 文件名") : QStringLiteral("Fc 回退"),
        m_hasSpanMetadata ? QStringLiteral("Bw 文件名") : QStringLiteral("Bw 回退"),
        m_hasRbwMetadata ? QStringLiteral("Rbw 文件名") : QStringLiteral("Rbw 回退"),
        m_hasReferenceMetadata ? QStringLiteral("Reflevel 文件名") : QStringLiteral("Reflevel 回退"),
        m_hasPointMetadata ? QStringLiteral("SpectrumLen 文件名") : QStringLiteral("点数回退")};
    m_inputMetadata->setToolTip(QStringLiteral("当前使用：%1").arg(sourceFields.join(QStringLiteral("，"))));
}

void DetectionLabWindow::updateControls()
{
    const bool busy = m_openPending || m_isRunning || m_stopPending;
    const bool hasReference = m_referenceFile && !m_referenceFile->text().trimmed().isEmpty();
    m_inspectButton->setEnabled(!m_sessionOpened && !busy);
    m_startButton->setEnabled(!busy);
    m_startButton->setText(m_sessionOpened ? QStringLiteral("继续检测") : QStringLiteral("开始检测"));
    m_pauseButton->setEnabled(m_sessionOpened && m_isRunning);
    m_stepButton->setEnabled(m_sessionOpened && !m_isRunning && !hasReference);
    m_stopButton->setEnabled(m_sessionOpened || busy);
    m_seekButton->setEnabled(m_sessionOpened && !m_isRunning && !hasReference);
    m_leftTabs->setEnabled(!m_sessionOpened && !busy);
}

void DetectionLabWindow::appendLog(const QString& message)
{
    m_log->appendPlainText(QStringLiteral("[%1] %2")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")), message));
}

void DetectionLabWindow::showError(const QString& message)
{
    QMessageBox::critical(this, QStringLiteral("DetectionLab"), message);
}

void DetectionLabWindow::zoomToRow(QTableWidget* table)
{
    const int row = table->currentRow();
    auto* item = row >= 0 ? table->item(row, 0) : nullptr;
    if (!item) return;
    const double start = item->data(Qt::UserRole).toDouble();
    const double end = item->data(Qt::UserRole + 1).toDouble();
    const double width = end - start;
    if (!(width > 0.0)) return;
    const double pad = width * 0.25;
    const double spectrumStart = m_spectrum->viewStartFrequency();
    const double spectrumEnd = m_spectrum->viewEndFrequency();
    (void)spectrumStart;
    (void)spectrumEnd;
    m_spectrum->setFrequencyView(start - pad, end + pad);
}

QString DetectionLabWindow::formatFrequency(double hz) const
{
    return app::FrequencySpinBox::formatFrequency(static_cast<std::int64_t>(std::llround(hz)));
}

} // namespace scn::lab
