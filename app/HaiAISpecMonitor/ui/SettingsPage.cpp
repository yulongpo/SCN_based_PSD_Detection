#include "SettingsPage.h"
#include "FrequencySpinBox.h"

#include <QAbstractItemView>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
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
#include <QStandardPaths>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextStream>
#include <QToolButton>
#include <QVBoxLayout>
#include <QMessageBox>

#include "../../../application/policy/PolicyRepository.h"

#include <limits>
#include <algorithm>

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

QString defaultRecordingDirectory()
{
    return QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("data_record"));
}

QString legacyRecordingDirectory()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (base.isEmpty()) base = QDir::homePath();
    return QDir(base).filePath(QStringLiteral("HaiAISpecMonitor/recordings"));
}

class FrequencyItemDelegate final : public QStyledItemDelegate
{
public:
    explicit FrequencyItemDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent)
    {
    }

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem&,
                           const QModelIndex&) const override
    {
        auto* editor = new FrequencySpinBox(parent);
        editor->setRange(0.0, 6.4e9);
        editor->setDecimals(6);
        editor->setMinimumWidth(150);
        auto* delegate = const_cast<FrequencyItemDelegate*>(this);
        connect(editor, &QDoubleSpinBox::editingFinished, delegate, [delegate, editor] {
            emit delegate->commitData(editor);
            emit delegate->closeEditor(editor);
        });
        return editor;
    }

    void setEditorData(QWidget* editor, const QModelIndex& index) const override
    {
        auto* frequencyEditor = dynamic_cast<FrequencySpinBox*>(editor);
        if (!frequencyEditor) return;
        double value = 0.0;
        if (FrequencySpinBox::parseFrequencyText(index.data(Qt::EditRole).toString(), value))
            frequencyEditor->setValue(value);
    }

    void setModelData(QWidget* editor, QAbstractItemModel* model,
                      const QModelIndex& index) const override
    {
        auto* frequencyEditor = dynamic_cast<FrequencySpinBox*>(editor);
        if (!frequencyEditor) return;
        frequencyEditor->interpretText();
        model->setData(index, FrequencySpinBox::formatFrequency(frequencyEditor->value()),
                       Qt::EditRole);
    }
};
}

SettingsPage::SettingsPage(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
    loadDetectionConfig();
    loadPolicyConfig();
}

int SettingsPage::displayRefreshRateHz() const
{
    return m_displayRate ? m_displayRate->value() : 30;
}

application::RecordingConfig SettingsPage::recordingConfig() const
{
    application::RecordingConfig config;
    config.enabled = m_recordingEnabled && m_recordingEnabled->isChecked();
    const QString directory = m_recordingDirectory
        ? m_recordingDirectory->text().trimmed() : QString();
    config.directory = (directory.isEmpty() ? defaultRecordingDirectory() : directory).toStdString();
    return config;
}

void SettingsPage::saveRecordingConfig() const
{
    const auto config = recordingConfig();
    QSettings settings(QStringLiteral("SCN"), QStringLiteral("HaiAISpecMonitor"));
    settings.setValue(QStringLiteral("recording/enabled"), config.enabled);
    settings.setValue(QStringLiteral("recording/directory"),
                      QString::fromStdString(config.directory));
    settings.setValue(QStringLiteral("recording/directoryVersion"), 2);
    settings.sync();
}

void SettingsPage::setRecordingStatus(const QString& status)
{
    if (m_recordingStatus) m_recordingStatus->setText(status);
}

void SettingsPage::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);

    auto* nav = new QHBoxLayout;
    const QStringList names = {QStringLiteral("显示"), QStringLiteral("存储"),
                               QStringLiteral("白名单"), QStringLiteral("告警规则"),
                               QStringLiteral("告警历史"),
                               QStringLiteral("推送"),
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
    m_stack->addWidget(buildWhitelistPage());
    m_stack->addWidget(buildRulePage());
    m_stack->addWidget(buildHistoryPage());
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
    m_displayRate = new QSpinBox(card);
    m_displayRate->setRange(1, 120);
    m_displayRate->setValue(30);
    m_displayRate->setSuffix(QStringLiteral(" fps"));
    m_displayRate->setMinimumWidth(120);
    m_displayRate->setToolTip(QStringLiteral(
        "设置界面和会话结果发布频率；数据源采集频率由源参数单独控制。"));
    form->addRow(QStringLiteral("显示刷新"), m_displayRate);
    const QSettings settings(QStringLiteral("SCN"), QStringLiteral("HaiAISpecMonitor"));
    m_displayRate->setValue(settings.value(QStringLiteral("ui/displayRefreshRateHz"), 30).toInt());
    connect(m_displayRate, qOverload<int>(&QSpinBox::valueChanged), this, [this](int rateHz) {
        const int safeRateHz = std::clamp(rateHz, 1, 120);
        QSettings settings(QStringLiteral("SCN"), QStringLiteral("HaiAISpecMonitor"));
        settings.setValue(QStringLiteral("ui/displayRefreshRateHz"), safeRateHz);
        settings.sync();
        emit displayRefreshRateChanged(safeRateHz);
    });
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
    m_fusionGap = new FrequencySpinBox(card);
    m_fusionGap->setRange(0.0, 6.4e9);
    m_fusionGap->setDecimals(6);
    m_fusionGap->setMinimumWidth(150);
    form->addRow(QStringLiteral("融合间隔"), m_fusionGap);
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

    auto* recordingTitle = new QLabel(QStringLiteral("实时源录制"), card);
    recordingTitle->setObjectName(QStringLiteral("settingsSectionTitle"));
    form->addRow(recordingTitle);
    m_recordingEnabled = new QCheckBox(QStringLiteral("监测开始时录制 BB60C / 海得罗捷频谱"), card);
    form->addRow(QStringLiteral("录制开关"), m_recordingEnabled);
    m_recordingDirectory = new QLineEdit(card);
    auto* recordingBrowse = actionButton(QStringLiteral("选择目录"), card);
    auto* recordingPathRow = new QHBoxLayout;
    recordingPathRow->addWidget(m_recordingDirectory, 1);
    recordingPathRow->addWidget(recordingBrowse);
    form->addRow(QStringLiteral("录制目录"), recordingPathRow);
    auto* recordingFormat = new QLabel(QStringLiteral(
        "固定保存为连续 little-endian float32 PSD 帧（.dat），文件名包含 "
        "Fc、Bw、Rbw、Reflevel 和 SpectrumLen，可由 FILE 源直接回放。"), card);
    recordingFormat->setWordWrap(true);
    recordingFormat->setObjectName(QStringLiteral("pageHint"));
    form->addRow(QString(), recordingFormat);
    m_recordingStatus = new QLabel(QStringLiteral("未开始录制"), card);
    m_recordingStatus->setObjectName(QStringLiteral("pageHint"));
    form->addRow(QStringLiteral("录制状态"), m_recordingStatus);

    const QSettings settings(QStringLiteral("SCN"), QStringLiteral("HaiAISpecMonitor"));
    m_recordingEnabled->setChecked(
        settings.value(QStringLiteral("recording/enabled"), false).toBool());
    QString recordingDirectory = settings.value(
        QStringLiteral("recording/directory")).toString().trimmed();
    if (recordingDirectory.isEmpty() ||
        QDir::cleanPath(recordingDirectory) == QDir::cleanPath(legacyRecordingDirectory())) {
        recordingDirectory = defaultRecordingDirectory();
    }
    m_recordingDirectory->setText(recordingDirectory);
    m_recordingDirectory->setToolTip(QStringLiteral(
        "默认目录为程序路径下的 data_record，可按需修改。"));

    auto* apply = actionButton(QStringLiteral("应用存储策略"), card);
    form->addRow(QString(), apply);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getExistingDirectory(this, QStringLiteral("选择频谱存储目录"));
        if (!path.isEmpty()) m_storagePath->setText(path);
    });
    connect(recordingBrowse, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getExistingDirectory(
            this, QStringLiteral("选择实时频谱录制目录"), m_recordingDirectory->text());
        if (!path.isEmpty()) m_recordingDirectory->setText(path);
    });
    connect(apply, &QPushButton::clicked, this, &SettingsPage::applyStorage);
    layout->addWidget(card, 0, Qt::AlignTop);
    layout->addStretch();
    return page;
}

QWidget* SettingsPage::buildWhitelistPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* toolbar = new QHBoxLayout;
    toolbar->addWidget(new QLabel(QStringLiteral("白名单（命中后替换为配置频段，不抑制告警）"), page));
    toolbar->addStretch();
    auto* add = actionButton(QStringLiteral("新增"), page);
    auto* remove = actionButton(QStringLiteral("删除"), page);
    auto* import = actionButton(QStringLiteral("导入"), page);
    auto* exportButton = actionButton(QStringLiteral("导出"), page);
    toolbar->addWidget(add); toolbar->addWidget(remove);
    toolbar->addWidget(import); toolbar->addWidget(exportButton);
    layout->addLayout(toolbar);
    m_whitelistTable = new QTableWidget(0, 5, page);
    m_whitelistTable->setObjectName(QStringLiteral("isaTable"));
    m_whitelistTable->setHorizontalHeaderLabels({QStringLiteral("名称"), QStringLiteral("起始频率"),
        QStringLiteral("终止频率"), QStringLiteral("启用"), QStringLiteral("备注")});
    m_whitelistTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_whitelistTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_whitelistTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_whitelistTable->setEditTriggers(QAbstractItemView::DoubleClicked |
                                      QAbstractItemView::EditKeyPressed);
    auto* whitelistFrequencyDelegate = new FrequencyItemDelegate(m_whitelistTable);
    m_whitelistTable->setItemDelegateForColumn(1, whitelistFrequencyDelegate);
    m_whitelistTable->setItemDelegateForColumn(2, whitelistFrequencyDelegate);
    layout->addWidget(m_whitelistTable, 1);
    auto* apply = actionButton(QStringLiteral("应用白名单与告警规则"), page);
    layout->addWidget(apply, 0, Qt::AlignRight);
    connect(add, &QPushButton::clicked, this, &SettingsPage::addWhitelist);
    connect(remove, &QPushButton::clicked, this, &SettingsPage::removeWhitelist);
    connect(import, &QPushButton::clicked, this, &SettingsPage::importPolicy);
    connect(exportButton, &QPushButton::clicked, this, &SettingsPage::exportPolicy);
    connect(apply, &QPushButton::clicked, this, &SettingsPage::applyPolicy);
    return page;
}

QWidget* SettingsPage::buildRulePage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* toolbar = new QHBoxLayout;
    toolbar->addWidget(new QLabel(QStringLiteral("告警规则（相交匹配，最高等级生效）"), page));
    toolbar->addStretch();
    auto* add = actionButton(QStringLiteral("新增规则"), page);
    auto* remove = actionButton(QStringLiteral("删除规则"), page);
    auto* import = actionButton(QStringLiteral("导入"), page);
    auto* exportButton = actionButton(QStringLiteral("导出"), page);
    toolbar->addWidget(add);
    toolbar->addWidget(remove);
    toolbar->addWidget(import);
    toolbar->addWidget(exportButton);
    layout->addLayout(toolbar);
    m_ruleTable = new QTableWidget(0, 13, page);
    m_ruleTable->setObjectName(QStringLiteral("isaTable"));
    m_ruleTable->setHorizontalHeaderLabels({QStringLiteral("规则名称"), QStringLiteral("起始频率"),
        QStringLiteral("终止频率"), QStringLiteral("最小带宽"), QStringLiteral("最大带宽"),
        QStringLiteral("最小电平(dBm)"), QStringLiteral("最小 CNR(dB)"), QStringLiteral("最小置信度"),
        QStringLiteral("等级"), QStringLiteral("连续次数"), QStringLiteral("持续(s)"),
        QStringLiteral("解除(s)"), QStringLiteral("启用")});
    m_ruleTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_ruleTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_ruleTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_ruleTable->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    auto* ruleFrequencyDelegate = new FrequencyItemDelegate(m_ruleTable);
    for (int column = 1; column <= 4; ++column)
        m_ruleTable->setItemDelegateForColumn(column, ruleFrequencyDelegate);
    layout->addWidget(m_ruleTable, 1);
    connect(add, &QPushButton::clicked, this, &SettingsPage::addRule);
    connect(remove, &QPushButton::clicked, this, &SettingsPage::removeRule);
    connect(import, &QPushButton::clicked, this, &SettingsPage::importPolicy);
    connect(exportButton, &QPushButton::clicked, this, &SettingsPage::exportPolicy);
    auto* apply = actionButton(QStringLiteral("应用白名单与告警规则"), page);
    layout->addWidget(apply, 0, Qt::AlignRight);
    connect(apply, &QPushButton::clicked, this, &SettingsPage::applyPolicy);
    return page;
}

QWidget* SettingsPage::buildHistoryPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* card = formContainer(page);
    auto* form = new QFormLayout(card);
    form->setContentsMargins(22, 22, 22, 22);
    form->addRow(new QLabel(QStringLiteral("本地告警历史"), card));
    form->addRow(QStringLiteral("存储方式"), new QLabel(QStringLiteral("SQLite（事件状态可追溯）"), card));
    form->addRow(QStringLiteral("匹配语义"), new QLabel(QStringLiteral("频段相交且端点相接即命中；白名单不抑制告警"), card));
    auto* view = actionButton(QStringLiteral("查询 / 确认 / 导出历史"), card);
    form->addRow(QString(), view);
    connect(view, &QPushButton::clicked, this, &SettingsPage::alarmHistoryRequested);
    layout->addWidget(card, 0, Qt::AlignTop);
    layout->addStretch();
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
        "信号表展示白名单整理后的业务结果，双击可查看代表检测值、归并原始 ID、白名单和告警状态；信号类型未分类。\n"
        "命中白名单的原始结果会替换为配置频段，不会免除告警；告警历史保存在应用目录 config/policy.sqlite。\n"
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
    const QStringList values = {QStringLiteral("新规则"), FrequencySpinBox::formatFrequency(0.0),
        FrequencySpinBox::formatFrequency(1.0e9), FrequencySpinBox::formatFrequency(0.0),
        FrequencySpinBox::formatFrequency(0.0), QStringLiteral("—"), QStringLiteral("—"),
        QStringLiteral("—"), QStringLiteral("一般"), QStringLiteral("1"), QStringLiteral("0"),
        QStringLiteral("1"), QStringLiteral("是")};
    for (int column = 0; column < values.size(); ++column)
        m_ruleTable->setItem(row, column, new QTableWidgetItem(values.at(column)));
    qlonglong nextId = 1;
    for (int i = 0; i < row; ++i) nextId = std::max(nextId, m_ruleTable->item(i, 0)->data(Qt::UserRole).toLongLong() + 1);
    m_ruleTable->item(row, 0)->setData(Qt::UserRole, nextId);
    appendLog(QStringLiteral("新增一条告警规则。"));
}

void SettingsPage::removeRule()
{
    const int row = m_ruleTable->currentRow();
    if (row < 0) return;
    m_ruleTable->removeRow(row);
    appendLog(QStringLiteral("删除一条告警规则。"));
}

void SettingsPage::addWhitelist()
{
    const int row = m_whitelistTable->rowCount();
    m_whitelistTable->insertRow(row);
    const QStringList values = {QStringLiteral("新白名单"), FrequencySpinBox::formatFrequency(0.0),
        FrequencySpinBox::formatFrequency(1.0e9), QStringLiteral("是"), QString()};
    for (int column = 0; column < values.size(); ++column)
        m_whitelistTable->setItem(row, column, new QTableWidgetItem(values.at(column)));
    qlonglong nextId = 1;
    for (int i = 0; i < row; ++i) nextId = std::max(nextId, m_whitelistTable->item(i, 0)->data(Qt::UserRole).toLongLong() + 1);
    m_whitelistTable->item(row, 0)->setData(Qt::UserRole, nextId);
    appendLog(QStringLiteral("新增一条白名单。"));
}

void SettingsPage::removeWhitelist()
{
    const int row = m_whitelistTable->currentRow();
    if (row < 0) return;
    m_whitelistTable->removeRow(row);
    appendLog(QStringLiteral("删除一条白名单。"));
}

policy::PolicyConfig SettingsPage::policyConfig(QString* error) const
{
    policy::PolicyConfig config;
    config.version = static_cast<std::uint64_t>(QSettings().value(QStringLiteral("policy/version"), 1).toULongLong());
    auto parse = [](const QTableWidget* table, int row, int column, double fallback = 0.0) {
        const auto text = table->item(row, column) ? table->item(row, column)->text() : QString();
        double value = fallback;
        return FrequencySpinBox::parseFrequencyText(text, value)
            ? value : std::numeric_limits<double>::quiet_NaN();
    };
    for (int row = 0; row < m_whitelistTable->rowCount(); ++row) {
        policy::WhitelistEntry item;
        item.id = m_whitelistTable->item(row, 0)->data(Qt::UserRole).toLongLong();
        if (item.id == 0) item.id = row + 1;
        item.name = m_whitelistTable->item(row, 0)->text().toStdString();
        item.startFrequencyHz = parse(m_whitelistTable, row, 1);
        item.endFrequencyHz = parse(m_whitelistTable, row, 2);
        item.enabled = m_whitelistTable->item(row, 3)->text().trimmed() != QStringLiteral("否");
        item.note = m_whitelistTable->item(row, 4)->text().toStdString();
        config.whitelists.push_back(std::move(item));
    }
    auto parseOptional = [&](const QTableWidget* table, int row, int column, bool& enabled) {
        const auto text = table->item(row, column) ? table->item(row, column)->text().trimmed() : QString();
        if (text.isEmpty() || text == QStringLiteral("—") || text == QStringLiteral("-")) {
            enabled = false; return 0.0;
        }
        bool ok = false; const double value = text.toDouble(&ok);
        enabled = true; return ok ? value : std::numeric_limits<double>::quiet_NaN();
    };
    for (int row = 0; row < m_ruleTable->rowCount(); ++row) {
        policy::AlarmRule rule;
        rule.id = m_ruleTable->item(row, 0)->data(Qt::UserRole).toLongLong();
        if (rule.id == 0) rule.id = row + 1;
        rule.name = m_ruleTable->item(row, 0)->text().toStdString();
        rule.startFrequencyHz = parse(m_ruleTable, row, 1);
        rule.endFrequencyHz = parse(m_ruleTable, row, 2);
        rule.minBandwidthHz = parse(m_ruleTable, row, 3);
        rule.maxBandwidthHz = parse(m_ruleTable, row, 4);
        rule.minSignalLevelDbm = static_cast<float>(parseOptional(m_ruleTable, row, 5, rule.useMinSignalLevel));
        rule.minCnrDb = static_cast<float>(parseOptional(m_ruleTable, row, 6, rule.useMinCnr));
        rule.minConfidence = static_cast<float>(parseOptional(m_ruleTable, row, 7, rule.useMinConfidence));
        rule.level = m_ruleTable->item(row, 8)->text().contains(QStringLiteral("严重"))
            ? policy::AlarmLevel::Critical : policy::AlarmLevel::General;
        rule.consecutiveHits = static_cast<std::uint32_t>(m_ruleTable->item(row, 9)->text().toUInt());
        rule.minDurationSeconds = parse(m_ruleTable, row, 10);
        rule.clearDelaySeconds = parse(m_ruleTable, row, 11);
        rule.enabled = m_ruleTable->item(row, 12)->text().trimmed() != QStringLiteral("否");
        config.alarmRules.push_back(std::move(rule));
    }
    std::string validationError;
    if (!policy::PolicyRepository::validate(config, validationError) && error)
        *error = QString::fromStdString(validationError);
    return config;
}

void SettingsPage::populatePolicyTables(const policy::PolicyConfig& config)
{
    m_whitelistTable->setRowCount(0);
    for (const auto& item : config.whitelists) {
        const int row = m_whitelistTable->rowCount(); m_whitelistTable->insertRow(row);
        const QStringList values = {QString::fromStdString(item.name),
            FrequencySpinBox::formatFrequency(item.startFrequencyHz),
            FrequencySpinBox::formatFrequency(item.endFrequencyHz),
            item.enabled ? QStringLiteral("是") : QStringLiteral("否"),
            QString::fromStdString(item.note)};
        for (int column = 0; column < values.size(); ++column)
            m_whitelistTable->setItem(row, column, new QTableWidgetItem(values.at(column)));
        m_whitelistTable->item(row, 0)->setData(Qt::UserRole, item.id);
    }
    m_ruleTable->setRowCount(0);
    for (const auto& rule : config.alarmRules) {
        const int row = m_ruleTable->rowCount(); m_ruleTable->insertRow(row);
        const QStringList values = {QString::fromStdString(rule.name),
            FrequencySpinBox::formatFrequency(rule.startFrequencyHz),
            FrequencySpinBox::formatFrequency(rule.endFrequencyHz),
            FrequencySpinBox::formatFrequency(rule.minBandwidthHz),
            FrequencySpinBox::formatFrequency(rule.maxBandwidthHz),
            rule.useMinSignalLevel ? QString::number(rule.minSignalLevelDbm, 'f', 3) : QStringLiteral("—"),
            rule.useMinCnr ? QString::number(rule.minCnrDb, 'f', 3) : QStringLiteral("—"),
            rule.useMinConfidence ? QString::number(rule.minConfidence, 'f', 3) : QStringLiteral("—"),
            rule.level == policy::AlarmLevel::Critical ? QStringLiteral("严重") : QStringLiteral("一般"),
            QString::number(rule.consecutiveHits), QString::number(rule.minDurationSeconds, 'f', 3),
            QString::number(rule.clearDelaySeconds, 'f', 3), rule.enabled ? QStringLiteral("是") : QStringLiteral("否")};
        for (int column = 0; column < values.size(); ++column)
            m_ruleTable->setItem(row, column, new QTableWidgetItem(values.at(column)));
        m_ruleTable->item(row, 0)->setData(Qt::UserRole, rule.id);
    }
    QSettings settings; settings.setValue(QStringLiteral("policy/version"), static_cast<qulonglong>(config.version));
}

void SettingsPage::loadPolicyConfig()
{
    policy::PolicyConfig config; std::string error;
    if (!policy::PolicyRepository::load(policy::PolicyRepository::defaultPath(), config, error)) {
        appendLog(QStringLiteral("告警规则加载失败：%1").arg(QString::fromStdString(error)));
        return;
    }
    populatePolicyTables(config);
}

bool SettingsPage::savePolicyConfig(const policy::PolicyConfig& config, QString* error)
{
    std::string reason;
    if (!policy::PolicyRepository::save(policy::PolicyRepository::defaultPath(), config, reason)) {
        const auto message = QString::fromStdString(reason);
        if (error) *error = message;
        appendLog(QStringLiteral("告警规则保存失败：%1").arg(message));
        return false;
    }
    populatePolicyTables(config);
    if (error) error->clear();
    return true;
}

bool SettingsPage::acceptPolicyConfig(const policy::PolicyConfig& config, QString* error)
{
    return savePolicyConfig(config, error);
}

void SettingsPage::applyPolicy()
{
    QString error;
    const auto config = policyConfig(&error);
    if (!error.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("规则参数无效"), error);
        return;
    }
    emit policyApplyRequested();
    appendLog(QStringLiteral("已提交白名单与告警规则版本 %1。").arg(config.version));
}

void SettingsPage::importPolicy()
{
    const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("导入规则"), QString(), QStringLiteral("JSON files (*.json)"));
    if (path.isEmpty()) return;
    policy::PolicyConfig config; std::string error;
    if (!policy::PolicyRepository::load(path.toStdString(), config, error)) {
        QMessageBox::warning(this, QStringLiteral("导入失败"), QString::fromStdString(error)); return;
    }
    populatePolicyTables(config); appendLog(QStringLiteral("已导入规则草稿。请点击应用。"));
}

void SettingsPage::exportPolicy()
{
    const auto path = QFileDialog::getSaveFileName(this, QStringLiteral("导出规则"), QStringLiteral("policy.json"), QStringLiteral("JSON files (*.json)"));
    if (path.isEmpty()) return;
    QString error; const auto config = policyConfig(&error); if (!error.isEmpty()) return;
    std::string reason;
    if (!policy::PolicyRepository::save(path.toStdString(), config, reason))
        QMessageBox::warning(this, QStringLiteral("导出失败"), QString::fromStdString(reason));
}

void SettingsPage::applyStorage()
{
    saveRecordingConfig();
    appendLog(QStringLiteral("存储策略已应用：%1").arg(m_storagePath->text()));
    appendLog(QStringLiteral("实时频谱录制已%1：%2")
        .arg(recordingConfig().enabled ? QStringLiteral("启用") : QStringLiteral("关闭"),
             QString::fromStdString(recordingConfig().directory)));
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
