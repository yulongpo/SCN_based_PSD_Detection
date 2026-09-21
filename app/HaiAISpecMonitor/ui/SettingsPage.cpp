#include "SettingsPage.h"

#include <QAbstractItemView>
#include <QButtonGroup>
#include <QCheckBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFile>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextStream>
#include <QToolButton>
#include <QVBoxLayout>

#include <limits>

namespace scn::app
{

namespace
{
QPushButton* navButton(const QString& text, QWidget* parent)
{
    auto* button = new QPushButton(text, parent);
    button->setCheckable(true);
    button->setFixedSize(82, 32);
    button->setObjectName(QStringLiteral("settingsNavButton"));
    return button;
}

QPushButton* actionButton(const QString& text, QWidget* parent)
{
    auto* button = new QPushButton(text, parent);
    button->setObjectName(QStringLiteral("pageToolButton"));
    button->setMinimumHeight(34);
    return button;
}

QWidget* formContainer(QWidget* parent)
{
    auto* widget = new QWidget(parent);
    widget->setObjectName(QStringLiteral("settingsCard"));
    return widget;
}
}

SettingsPage::SettingsPage(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
    loadDetectionConfig();
}

void SettingsPage::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);

    auto* nav = new QHBoxLayout;
    const QStringList names = {QStringLiteral("显示"), QStringLiteral("存储"),
                               QStringLiteral("规则"), QStringLiteral("推送"),
                               QStringLiteral("日志"), QStringLiteral("帮助"),
                               QStringLiteral("SCN 检测")};
    auto* group = new QButtonGroup(this);
    group->setExclusive(true);
    for (int i = 0; i < names.size(); ++i) {
        auto* button = navButton(names.at(i), this);
        group->addButton(button, i);
        nav->addWidget(button);
    }
    nav->addStretch();
    root->addLayout(nav);

    m_stack = new QStackedWidget(this);
    m_stack->addWidget(buildDisplayPage());
    m_stack->addWidget(buildStoragePage());
    m_stack->addWidget(buildRulePage());
    m_stack->addWidget(buildPushPage());
    m_stack->addWidget(buildLogPage());
    m_stack->addWidget(buildHelpPage());
    m_stack->addWidget(buildDetectionPage());
    root->addWidget(m_stack, 1);

    group->button(0)->setChecked(true);
    connect(group, &QButtonGroup::idClicked, this, &SettingsPage::selectPage);
}

QWidget* SettingsPage::buildDisplayPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* card = formContainer(page);
    auto* form = new QFormLayout(card);
    form->setContentsMargins(22, 22, 22, 22);
    form->setVerticalSpacing(16);
    form->addRow(new QLabel(QStringLiteral("显示设置"), card));
    auto* grid = new QCheckBox(QStringLiteral("显示频谱网格线"), card);
    grid->setChecked(true);
    form->addRow(QStringLiteral("网格"), grid);
    auto* marker = new QCheckBox(QStringLiteral("显示信号标记和频率提示"), card);
    marker->setChecked(true);
    form->addRow(QStringLiteral("标记"), marker);
    auto* maxHold = new QCheckBox(QStringLiteral("启用最大保持谱"), card);
    form->addRow(QStringLiteral("最大保持"), maxHold);
    auto* waterfall = new QCheckBox(QStringLiteral("显示瀑布图"), card);
    waterfall->setChecked(true);
    form->addRow(QStringLiteral("瀑布图"), waterfall);
    auto* rows = new QSpinBox(card);
    rows->setRange(50, 2000);
    rows->setValue(100);
    form->addRow(QStringLiteral("瀑布行数"), rows);
    auto* apply = actionButton(QStringLiteral("应用显示设置"), card);
    form->addRow(QString(), apply);
    connect(apply, &QPushButton::clicked, this, [this] {
        appendLog(QStringLiteral("显示设置已应用。"));
    });
    layout->addWidget(card, 0, Qt::AlignTop);
    layout->addStretch();
    return page;
}

QWidget* SettingsPage::buildDetectionPage()
{
    auto* page = new QScrollArea(this);
    page->setWidgetResizable(true);
    page->setFrameShape(QFrame::NoFrame);
    auto* card = formContainer(page);
    auto* form = new QFormLayout(card);
    form->setContentsMargins(22, 22, 22, 22);
    form->setVerticalSpacing(10);
    auto* note = new QLabel(QStringLiteral(
        "SCN 检测独立累计 16 帧（默认）；界面最大值／平均值仍使用原有 100 帧。\n"
        "更换模型、GPU 或切换检测开关前，请先停止监测。相对模型路径以程序目录为基准。"), card);
    note->setWordWrap(true);
    form->addRow(note);
    const auto integer = [card, form](const QString& title, int minimum, int maximum) {
        auto* field = new QSpinBox(card);
        field->setRange(minimum, maximum);
        form->addRow(title, field);
        return field;
    };
    const auto decimal = [card, form](const QString& title, double minimum, double maximum,
                                      int decimals = 4) {
        auto* field = new QDoubleSpinBox(card);
        field->setDecimals(decimals);
        field->setRange(minimum, maximum);
        field->setSingleStep(0.05);
        form->addRow(title, field);
        return field;
    };
    m_detectionEnabled = new QCheckBox(QStringLiteral("启用 SCN 检测"), card);
    form->addRow(QStringLiteral("检测开关"), m_detectionEnabled);
    m_modelPath = new QLineEdit(card);
    auto* browse = actionButton(QStringLiteral("选择模型"), card);
    auto* pathRow = new QHBoxLayout;
    pathRow->addWidget(m_modelPath, 1);
    pathRow->addWidget(browse);
    form->addRow(QStringLiteral("TensorRT engine 路径"), pathRow);
    connect(browse, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("选择 SCN 模型"),
            m_modelPath->text(), QStringLiteral("TensorRT engine (*.engine);;All files (*)"));
        if (!path.isEmpty()) m_modelPath->setText(path);
    });
    m_gpuIndex = integer(QStringLiteral("GPU 索引"), 0, std::numeric_limits<int>::max());
    m_accumulatorFrames = integer(QStringLiteral("检测累计帧数"), 1, 256);
    m_confidence = decimal(QStringLiteral("置信度阈值"), 0.0, 1.0);
    m_nmsIou = decimal(QStringLiteral("NMS IoU"), 0.0001, 1.0);
    m_topK = integer(QStringLiteral("TopK"), 1, 8192);
    m_maxCandidates = integer(QStringLiteral("每窗口最大候选数"), 1, 8192);
    form->addRow(QStringLiteral("模型输入长度"), new QLabel(QStringLiteral("32768（固定）"), card));
    m_windowStep = integer(QStringLiteral("窗口步长（频点）"), 1, 32768);
    m_windowOverlap = new QLabel(card);
    form->addRow(QStringLiteral("窗口重叠率"), m_windowOverlap);
    connect(m_windowStep, &QSpinBox::valueChanged, this, [this](int step) {
        m_windowOverlap->setText(QStringLiteral("%1%（由步长计算）")
            .arg(100.0 * (1.0 - static_cast<double>(step) / 32768.0), 0, 'f', 2));
    });
    m_cnr = decimal(QStringLiteral("CNR 门限（dB）"), -1000.0, 1000.0);
    m_fusionIou = decimal(QStringLiteral("融合 IoU"), 0.0001, 1.0);
    m_fusionOverlap = decimal(QStringLiteral("融合重叠率"), 0.0001, 1.0);
    m_fusionGap = decimal(QStringLiteral("融合间隔（Hz）"), 0.0, 1.0e12);
    m_trackOverlap = decimal(QStringLiteral("跟踪重叠率"), 0.0001, 1.0);
    m_maxMiss = decimal(QStringLiteral("最大漏检时间（秒）"), 0.0, 3600.0);
    m_maxSignals = integer(QStringLiteral("最大信号数"), 1, 65536);
    auto* apply = actionButton(QStringLiteral("应用 SCN 设置"), card);
    form->addRow(QString(), apply);
    m_detectionFeedback = new QLabel(card);
    m_detectionFeedback->setWordWrap(true);
    m_detectionFeedback->setTextFormat(Qt::PlainText);
    form->addRow(m_detectionFeedback);
    connect(apply, &QPushButton::clicked, this, &SettingsPage::detectionApplyRequested);
    page->setWidget(card);
    return page;
}

algorithm::DetectionConfig SettingsPage::detectionConfig() const
{
    algorithm::DetectionConfig c;
    c.enabled = m_detectionEnabled->isChecked();
    c.detector.modelPath = m_modelPath->text().trimmed().toStdString();
    c.detector.deviceIndex = m_gpuIndex->value();
    c.accumulator.frames = static_cast<std::size_t>(m_accumulatorFrames->value());
    c.detector.confidenceThreshold = static_cast<float>(m_confidence->value());
    c.detector.nmsIou = static_cast<float>(m_nmsIou->value());
    c.detector.topK = static_cast<std::size_t>(m_topK->value());
    c.detector.maxCandidatesPerWindow = static_cast<std::size_t>(m_maxCandidates->value());
    c.detector.inputLength = 32768;
    c.detector.windowStep = static_cast<std::size_t>(m_windowStep->value());
    c.refine.cnrThresholdDb = static_cast<float>(m_cnr->value());
    c.fusion.iou = m_fusionIou->value();
    c.fusion.overlapRatio = m_fusionOverlap->value();
    c.fusion.gapHz = m_fusionGap->value();
    c.tracker.overlapRatio = m_trackOverlap->value();
    c.tracker.maxMissSeconds = m_maxMiss->value();
    c.maxSignals = static_cast<std::size_t>(m_maxSignals->value());
    return c;
}

void SettingsPage::setDetectionConfig(const algorithm::DetectionConfig& c)
{
    m_detectionEnabled->setChecked(c.enabled);
    m_modelPath->setText(QString::fromStdString(c.detector.modelPath));
    m_gpuIndex->setValue(c.detector.deviceIndex);
    m_accumulatorFrames->setValue(static_cast<int>(c.accumulator.frames));
    m_confidence->setValue(c.detector.confidenceThreshold);
    m_nmsIou->setValue(c.detector.nmsIou);
    m_topK->setValue(static_cast<int>(c.detector.topK));
    m_maxCandidates->setValue(static_cast<int>(c.detector.maxCandidatesPerWindow));
    m_windowStep->setValue(static_cast<int>(c.detector.windowStep));
    m_windowOverlap->setText(QStringLiteral("%1%（由步长计算）")
        .arg(100.0 * (1.0 - static_cast<double>(c.detector.windowStep) / 32768.0), 0, 'f', 2));
    m_cnr->setValue(c.refine.cnrThresholdDb);
    m_fusionIou->setValue(c.fusion.iou);
    m_fusionOverlap->setValue(c.fusion.overlapRatio);
    m_fusionGap->setValue(c.fusion.gapHz);
    m_trackOverlap->setValue(c.tracker.overlapRatio);
    m_maxMiss->setValue(c.tracker.maxMissSeconds);
    m_maxSignals->setValue(static_cast<int>(c.maxSignals));
}

void SettingsPage::loadDetectionConfig()
{
    QSettings settings(QStringLiteral("SCN"), QStringLiteral("HaiAISpecMonitor"));
    settings.beginGroup(QStringLiteral("detection"));
    algorithm::DetectionConfig c;
    c.enabled = settings.value(QStringLiteral("enabled"), c.enabled).toBool();
    c.detector.modelPath = settings.value(QStringLiteral("modelPath"),
        QString::fromStdString(c.detector.modelPath)).toString().toStdString();
    c.detector.deviceIndex = settings.value(QStringLiteral("gpuIndex"), c.detector.deviceIndex).toInt();
    c.accumulator.frames = settings.value(QStringLiteral("accumulatorFrames"), 16).toULongLong();
    c.detector.confidenceThreshold = settings.value(QStringLiteral("confidenceThreshold"), 0.4).toFloat();
    c.detector.nmsIou = settings.value(QStringLiteral("nmsIou"), 0.5).toFloat();
    c.detector.topK = settings.value(QStringLiteral("topK"), 200).toULongLong();
    c.detector.maxCandidatesPerWindow = settings.value(QStringLiteral("maxCandidatesPerWindow"), 150).toULongLong();
    c.detector.inputLength = settings.value(QStringLiteral("inputLength"), 32768).toULongLong();
    c.detector.windowStep = settings.value(QStringLiteral("windowStep"), 16384).toULongLong();
    c.refine.cnrThresholdDb = settings.value(QStringLiteral("cnrThresholdDb"), 3.0).toFloat();
    c.fusion.iou = settings.value(QStringLiteral("fusionIou"), 0.1).toDouble();
    c.fusion.overlapRatio = settings.value(QStringLiteral("fusionOverlapRatio"), 0.5).toDouble();
    c.fusion.gapHz = settings.value(QStringLiteral("fusionGapHz"), 0.0).toDouble();
    c.tracker.overlapRatio = settings.value(QStringLiteral("trackOverlapRatio"), 0.45).toDouble();
    c.tracker.maxMissSeconds = settings.value(QStringLiteral("maxMissSeconds"), 1.0).toDouble();
    c.maxSignals = settings.value(QStringLiteral("maxSignals"), 4096).toULongLong();
    std::string error;
    if (!algorithm::validateConfig(c, error)) {
        c = algorithm::DetectionConfig{};
        setDetectionFeedback(QStringLiteral("已保存的 SCN 配置无效，已载入默认值：%1")
            .arg(QString::fromStdString(error)));
    }
    setDetectionConfig(c);
}

void SettingsPage::saveDetectionConfig(const algorithm::DetectionConfig& c) const
{
    QSettings settings(QStringLiteral("SCN"), QStringLiteral("HaiAISpecMonitor"));
    settings.beginGroup(QStringLiteral("detection"));
    settings.setValue(QStringLiteral("enabled"), c.enabled);
    settings.setValue(QStringLiteral("modelPath"), QString::fromStdString(c.detector.modelPath));
    settings.setValue(QStringLiteral("gpuIndex"), c.detector.deviceIndex);
    settings.setValue(QStringLiteral("accumulatorFrames"), static_cast<qulonglong>(c.accumulator.frames));
    settings.setValue(QStringLiteral("confidenceThreshold"), c.detector.confidenceThreshold);
    settings.setValue(QStringLiteral("nmsIou"), c.detector.nmsIou);
    settings.setValue(QStringLiteral("topK"), static_cast<qulonglong>(c.detector.topK));
    settings.setValue(QStringLiteral("maxCandidatesPerWindow"), static_cast<qulonglong>(c.detector.maxCandidatesPerWindow));
    settings.setValue(QStringLiteral("inputLength"), static_cast<qulonglong>(c.detector.inputLength));
    settings.setValue(QStringLiteral("windowStep"), static_cast<qulonglong>(c.detector.windowStep));
    settings.setValue(QStringLiteral("cnrThresholdDb"), c.refine.cnrThresholdDb);
    settings.setValue(QStringLiteral("fusionIou"), c.fusion.iou);
    settings.setValue(QStringLiteral("fusionOverlapRatio"), c.fusion.overlapRatio);
    settings.setValue(QStringLiteral("fusionGapHz"), c.fusion.gapHz);
    settings.setValue(QStringLiteral("trackOverlapRatio"), c.tracker.overlapRatio);
    settings.setValue(QStringLiteral("maxMissSeconds"), c.tracker.maxMissSeconds);
    settings.setValue(QStringLiteral("maxSignals"), static_cast<qulonglong>(c.maxSignals));
    settings.sync();
}

void SettingsPage::acceptDetectionConfig(const algorithm::DetectionConfig& config)
{
    // Only called after the session accepts the request; rejected drafts stay unsaved.
    setDetectionConfig(config);
    saveDetectionConfig(config);
}

void SettingsPage::setDetectionFeedback(const QString& message)
{
    m_detectionFeedback->setText(message);
    appendLog(message);
}

QWidget* SettingsPage::buildStoragePage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* card = formContainer(page);
    auto* form = new QFormLayout(card);
    form->setContentsMargins(22, 22, 22, 22);
    m_storagePath = new QLineEdit(QStringLiteral("./data/spectrum"), card);
    auto* browse = actionButton(QStringLiteral("选择目录"), card);
    auto* pathRow = new QHBoxLayout;
    pathRow->addWidget(m_storagePath, 1);
    pathRow->addWidget(browse);
    form->addRow(QStringLiteral("频谱存储路径"), pathRow);
    auto* autoSave = new QCheckBox(QStringLiteral("监测时自动保存频谱"), card);
    autoSave->setChecked(true);
    form->addRow(QStringLiteral("自动存储"), autoSave);
    auto* retention = new QSpinBox(card);
    retention->setRange(1, 3650);
    retention->setValue(30);
    retention->setSuffix(QStringLiteral(" 天"));
    form->addRow(QStringLiteral("保留周期"), retention);
    auto* apply = actionButton(QStringLiteral("应用存储策略"), card);
    form->addRow(QString(), apply);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getExistingDirectory(this, QStringLiteral("选择频谱存储目录"));
        if (!path.isEmpty()) m_storagePath->setText(path);
    });
    connect(apply, &QPushButton::clicked, this, &SettingsPage::applyStorage);
    layout->addWidget(card, 0, Qt::AlignTop);
    layout->addStretch();
    return page;
}

QWidget* SettingsPage::buildRulePage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* toolbar = new QHBoxLayout;
    toolbar->addWidget(new QLabel(QStringLiteral("告警规则"), page));
    toolbar->addStretch();
    auto* add = actionButton(QStringLiteral("新增规则"), page);
    auto* remove = actionButton(QStringLiteral("删除规则"), page);
    toolbar->addWidget(add);
    toolbar->addWidget(remove);
    layout->addLayout(toolbar);
    m_ruleTable = new QTableWidget(0, 7, page);
    m_ruleTable->setObjectName(QStringLiteral("isaTable"));
    m_ruleTable->setHorizontalHeaderLabels({QStringLiteral("规则名称"), QStringLiteral("频率范围"),
        QStringLiteral("带宽范围"), QStringLiteral("信号类型"), QStringLiteral("告警等级"),
        QStringLiteral("启用"), QStringLiteral("备注")});
    m_ruleTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_ruleTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_ruleTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_ruleTable->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    layout->addWidget(m_ruleTable, 1);
    connect(add, &QPushButton::clicked, this, &SettingsPage::addRule);
    connect(remove, &QPushButton::clicked, this, &SettingsPage::removeRule);
    return page;
}

QWidget* SettingsPage::buildPushPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* card = formContainer(page);
    auto* form = new QFormLayout(card);
    form->setContentsMargins(22, 22, 22, 22);
    auto* enable = new QCheckBox(QStringLiteral("启用告警推送"), card);
    form->addRow(QStringLiteral("推送状态"), enable);
    auto* endpoint = new QLineEdit(QStringLiteral("http://127.0.0.1:8080/api/alarm"), card);
    form->addRow(QStringLiteral("服务地址"), endpoint);
    auto* token = new QLineEdit(card);
    token->setEchoMode(QLineEdit::Password);
    form->addRow(QStringLiteral("访问令牌"), token);
    auto* test = actionButton(QStringLiteral("发送测试消息"), card);
    form->addRow(QString(), test);
    connect(test, &QPushButton::clicked, this, [this] {
        appendLog(QStringLiteral("已发送一条推送测试消息（接口占位）。"));
    });
    layout->addWidget(card, 0, Qt::AlignTop);
    layout->addStretch();
    return page;
}

QWidget* SettingsPage::buildLogPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    m_logEdit = new QPlainTextEdit(page);
    m_logEdit->setReadOnly(true);
    m_logEdit->setPlaceholderText(QStringLiteral("系统操作日志将在这里显示。"));
    layout->addWidget(m_logEdit, 1);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    auto* clear = actionButton(QStringLiteral("清空日志"), page);
    auto* exportButton = actionButton(QStringLiteral("导出日志"), page);
    buttons->addWidget(clear);
    buttons->addWidget(exportButton);
    layout->addLayout(buttons);
    connect(clear, &QPushButton::clicked, this, &SettingsPage::clearLog);
    connect(exportButton, &QPushButton::clicked, this, &SettingsPage::exportLog);
    return page;
}

QWidget* SettingsPage::buildHelpPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* text = new QPlainTextEdit(page);
    text->setReadOnly(true);
    text->setPlainText(QStringLiteral(
        "智能频谱监测仪\n\n"
        "采集监测：选择 BB60C、Harogic 或 FILE 数据源，配置频段后开始监测。\n"
        "录制回放：导入文本/CSV/ASC 或 float32 二进制频谱文件，双击记录可进入信号明细。\n"
        "系统设置：管理显示、存储、告警规则、推送、日志和 SCN 检测参数。\n\n"
        "SCN 使用 TensorRT engine，默认独立累计 16 帧；界面最大值／平均值仍为 100 帧。\n"
        "更换模型、GPU 或切换检测开关需先停止监测。模型故障时原始频谱仍可查看。\n"
        "信号表展示最新检测观测，双击可查看置信度、CNR、分支和跟踪时间；信号类型未分类，告警未接入。\n"
        "FILE 时间标为回放逻辑时间，硬件时间标为本轮相对时间。\n"
        "当前版本：Qt 6.11.1 / VS 2026。"));
    layout->addWidget(text);
    return page;
}

void SettingsPage::selectPage(int index)
{
    m_stack->setCurrentIndex(index);
}

void SettingsPage::addRule()
{
    const int row = m_ruleTable->rowCount();
    m_ruleTable->insertRow(row);
    const QStringList values = {QStringLiteral("新规则"), QStringLiteral("全频段"),
        QStringLiteral("0 - 10000 kHz"), QStringLiteral("未知"), QStringLiteral("一般"),
        QStringLiteral("是"), QStringLiteral("待配置")};
    for (int column = 0; column < values.size(); ++column)
        m_ruleTable->setItem(row, column, new QTableWidgetItem(values.at(column)));
    appendLog(QStringLiteral("新增一条告警规则。"));
}

void SettingsPage::removeRule()
{
    const int row = m_ruleTable->currentRow();
    if (row < 0) return;
    m_ruleTable->removeRow(row);
    appendLog(QStringLiteral("删除一条告警规则。"));
}

void SettingsPage::applyStorage()
{
    appendLog(QStringLiteral("存储策略已应用：%1").arg(m_storagePath->text()));
}

void SettingsPage::appendLog(const QString& message)
{
    if (m_logEdit) {
        m_logEdit->appendPlainText(QStringLiteral("[%1] %2")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss")), message));
    }
    emit logMessage(message);
}

void SettingsPage::clearLog()
{
    if (m_logEdit) m_logEdit->clear();
}

void SettingsPage::exportLog()
{
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出日志"),
        QStringLiteral("hai_ai_spec_monitor.log"), QStringLiteral("Log files (*.log *.txt)"));
    if (path.isEmpty() || !m_logEdit) return;
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream stream(&file);
        stream << m_logEdit->toPlainText();
        emit logMessage(QStringLiteral("已导出系统日志：%1").arg(path));
    }
}

} // namespace scn::app
