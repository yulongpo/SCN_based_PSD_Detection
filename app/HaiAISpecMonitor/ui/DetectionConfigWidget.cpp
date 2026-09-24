#include "DetectionConfigWidget.h"
#include "FrequencySpinBox.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>

namespace scn::app
{

namespace
{
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

DetectionConfigWidget::DetectionConfigWidget(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
    setConfig(algorithm::DetectionConfig{});
}

void DetectionConfigWidget::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
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
    m_modelPath->setObjectName(QStringLiteral("detectionModelPath"));
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
    m_fusionGap->setDecimals(0);
    m_fusionGap->setMinimumWidth(150);
    form->addRow(QStringLiteral("融合间隔"), m_fusionGap);
    m_trackOverlap = decimal(QStringLiteral("跟踪重叠率"), 0.0001, 1.0);
    m_maxMiss = decimal(QStringLiteral("最大漏检时间（秒）"), 0.0, 3600.0);

    auto* boundaryGroup = new QGroupBox(QStringLiteral("跟踪与边界稳定"), card);
    auto* boundaryForm = new QFormLayout(boundaryGroup);
    m_boundaryStability = new QCheckBox(QStringLiteral("启用短窗口边界稳定与突变确认"), boundaryGroup);
    boundaryForm->addRow(QStringLiteral("稳定功能"), m_boundaryStability);
    const auto boundaryDecimal = [boundaryGroup, boundaryForm](const QString& label,
                                                                 double minimum, double maximum,
                                                                 int decimals, double step) {
        auto* value = new QDoubleSpinBox(boundaryGroup);
        value->setRange(minimum, maximum);
        value->setDecimals(decimals);
        value->setSingleStep(step);
        boundaryForm->addRow(label, value);
        return value;
    };
    m_trackMaxBandwidthRatio = boundaryDecimal(QStringLiteral("最大带宽比"), 1.0, 100.0, 2, 0.1);
    m_trackCenterDistanceRatio = boundaryDecimal(QStringLiteral("中心距离比例"), 0.001, 10.0, 3, 0.05);
    m_trackMedianWindow = new QSpinBox(boundaryGroup);
    m_trackMedianWindow->setRange(1, 31);
    boundaryForm->addRow(QStringLiteral("中位数历史窗口"), m_trackMedianWindow);
    m_trackSmoothingAlpha = boundaryDecimal(QStringLiteral("EMA 平滑系数 α"), 0.001, 1.0, 3, 0.05);
    m_trackJumpConfirmations = new QSpinBox(boundaryGroup);
    m_trackJumpConfirmations->setRange(2, 20);
    boundaryForm->addRow(QStringLiteral("突变确认次数"), m_trackJumpConfirmations);
    boundaryGroup->setToolTip(QStringLiteral(
        "正常观测经短窗口中位数与 EMA 稳定；大幅边界变化需连续确认。确认期间仍保留原稳定频段。"));
    form->addRow(boundaryGroup);

    auto* advancedGroup = new QGroupBox(QStringLiteral("高级：突变候选一致性容差"), card);
    auto* advancedForm = new QFormLayout(advancedGroup);
    const auto advancedDecimal = [advancedGroup, advancedForm](const QString& label,
                                                                 double minimum, double maximum,
                                                                 int decimals, double step) {
        auto* value = new QDoubleSpinBox(advancedGroup);
        value->setRange(minimum, maximum);
        value->setDecimals(decimals);
        value->setSingleStep(step);
        advancedForm->addRow(label, value);
        return value;
    };
    m_trackJumpEdgeChangeRatio = advancedDecimal(QStringLiteral("边界变化门限／轨迹带宽"), 0.001, 10.0, 3, 0.05);
    m_trackJumpCenterToleranceRatio = advancedDecimal(QStringLiteral("候选中心容差／带宽"), 0.001, 10.0, 3, 0.02);
    m_trackJumpBandwidthToleranceRatio = advancedDecimal(QStringLiteral("候选带宽比上限"), 1.0, 10.0, 2, 0.05);
    advancedGroup->setToolTip(QStringLiteral("频点网格容差仍固定为 2 个 bin；以下参数控制突变边界阈值和候选一致性。"));
    form->addRow(advancedGroup);

    auto* aggregationGroup = new QGroupBox(QStringLiteral("宽带信道聚合"), card);
    auto* aggregationForm = new QFormLayout(aggregationGroup);
    auto* aggregationNote = new QLabel(QStringLiteral(
        "将有持续占用证据支持的 SCN 碎片聚合为信道；先验只提供归属提示，不会单独触发检测或强制标称带宽。"),
        aggregationGroup);
    aggregationNote->setWordWrap(true);
    aggregationForm->addRow(aggregationNote);
    m_channelAggregationEnabled = new QCheckBox(QStringLiteral("启用占用证据与信道级聚合"), aggregationGroup);
    aggregationForm->addRow(QStringLiteral("功能开关"), m_channelAggregationEnabled);
    m_channelMaximumBandwidth = new FrequencySpinBox(aggregationGroup);
    m_channelMaximumBandwidth->setRange(1.0, 6.4e9);
    aggregationForm->addRow(QStringLiteral("最大自动聚合带宽"), m_channelMaximumBandwidth);
    const auto aggregationDecimal = [aggregationGroup, aggregationForm](const QString& label,
                                                                        double minimum, double maximum,
                                                                        int decimals, double step) {
        auto* value = new QDoubleSpinBox(aggregationGroup);
        value->setRange(minimum, maximum);
        value->setDecimals(decimals);
        value->setSingleStep(step);
        aggregationForm->addRow(label, value);
        return value;
    };
    m_channelHighThreshold = aggregationDecimal(QStringLiteral("占用建立门限（高于噪底 dB）"), 0.1, 100.0, 2, 0.5);
    m_channelLowThreshold = aggregationDecimal(QStringLiteral("占用延伸门限（高于噪底 dB）"), -100.0, 99.9, 2, 0.5);
    m_channelMinimumSupport = aggregationDecimal(QStringLiteral("历史支持比例"), 0.001, 1.0, 3, 0.05);
    m_channelMinimumCoverage = aggregationDecimal(QStringLiteral("聚合区域最小占用覆盖率"), 0.001, 1.0, 3, 0.05);
    const auto aggregationInteger = [aggregationGroup, aggregationForm](const QString& label,
                                                                         int minimum, int maximum) {
        auto* value = new QSpinBox(aggregationGroup);
        value->setRange(minimum, maximum);
        aggregationForm->addRow(label, value);
        return value;
    };
    m_channelMergeConfirmations = aggregationInteger(QStringLiteral("新聚合确认次数"), 2, 20);
    m_channelSplitConfirmations = aggregationInteger(QStringLiteral("信道拆分确认次数"), 2, 40);
    m_channelMissingConfirmations = aggregationInteger(QStringLiteral("可靠缺失确认次数"), 1, 20);
    m_channelMissingHold = aggregationDecimal(QStringLiteral("漏检保持时间（秒）"), 0.0, 3600.0, 3, 0.1);
    m_channelHistorySeconds = aggregationDecimal(QStringLiteral("占用证据最大时间跨度（秒）"), 0.05, 3600.0, 2, 0.1);
    m_channelPriorTable = new QTableWidget(0, 5, aggregationGroup);
    m_channelPriorTable->setHorizontalHeaderLabels({QStringLiteral("ID"), QStringLiteral("名称"),
        QStringLiteral("起始频率"), QStringLiteral("终止频率"), QStringLiteral("启用")});
    m_channelPriorTable->setColumnHidden(0, true);
    m_channelPriorTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_channelPriorTable->verticalHeader()->setVisible(false);
    m_channelPriorTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_channelPriorTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_channelPriorTable->setMaximumHeight(190);
    auto* priorTools = new QHBoxLayout;
    auto* addPrior = actionButton(QStringLiteral("新增信道先验"), aggregationGroup);
    auto* removePrior = actionButton(QStringLiteral("删除所选先验"), aggregationGroup);
    priorTools->addWidget(addPrior);
    priorTools->addWidget(removePrior);
    priorTools->addStretch();
    aggregationForm->addRow(QStringLiteral("可选先验（频率支持单位输入）"), m_channelPriorTable);
    aggregationForm->addRow(QString(), priorTools);
    connect(addPrior, &QPushButton::clicked, this, &DetectionConfigWidget::addChannelPrior);
    connect(removePrior, &QPushButton::clicked, this, &DetectionConfigWidget::removeChannelPrior);
    form->addRow(aggregationGroup);

    m_maxSignals = integer(QStringLiteral("最大信号数"), 1, 65536);
    auto* apply = actionButton(QStringLiteral("应用 SCN 设置"), card);
    form->addRow(QString(), apply);
    m_feedback = new QLabel(card);
    m_feedback->setWordWrap(true);
    m_feedback->setTextFormat(Qt::PlainText);
    form->addRow(m_feedback);
    connect(apply, &QPushButton::clicked, this, &DetectionConfigWidget::applyRequested);
    page->setWidget(card);
    root->addWidget(page);
}

algorithm::DetectionConfig DetectionConfigWidget::config() const
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
    c.fusion.gapHz = m_fusionGap->frequencyHz();
    c.tracker.overlapRatio = m_trackOverlap->value();
    c.tracker.maxMissSeconds = m_maxMiss->value();
    c.tracker.boundaryStabilityEnabled = m_boundaryStability->isChecked();
    c.tracker.maxBandwidthRatio = m_trackMaxBandwidthRatio->value();
    c.tracker.centerDistanceRatio = m_trackCenterDistanceRatio->value();
    c.tracker.medianWindow = static_cast<std::size_t>(m_trackMedianWindow->value());
    c.tracker.smoothingAlpha = m_trackSmoothingAlpha->value();
    c.tracker.jumpConfirmationCount = static_cast<std::size_t>(m_trackJumpConfirmations->value());
    c.tracker.jumpEdgeChangeRatio = m_trackJumpEdgeChangeRatio->value();
    c.tracker.jumpCenterToleranceRatio = m_trackJumpCenterToleranceRatio->value();
    c.tracker.jumpBandwidthToleranceRatio = m_trackJumpBandwidthToleranceRatio->value();
    c.channelAggregation.enabled = m_channelAggregationEnabled->isChecked();
    c.channelAggregation.maximumAutomaticBandwidthHz = m_channelMaximumBandwidth->frequencyHz();
    c.channelAggregation.highThresholdDb = m_channelHighThreshold->value();
    c.channelAggregation.lowThresholdDb = m_channelLowThreshold->value();
    c.channelAggregation.minimumSupportRatio = m_channelMinimumSupport->value();
    c.channelAggregation.minimumCoverageRatio = m_channelMinimumCoverage->value();
    c.channelAggregation.mergeConfirmationCount = static_cast<std::uint32_t>(m_channelMergeConfirmations->value());
    c.channelAggregation.splitConfirmationCount = static_cast<std::uint32_t>(m_channelSplitConfirmations->value());
    c.channelAggregation.missingConfirmationCount = static_cast<std::uint32_t>(m_channelMissingConfirmations->value());
    c.channelAggregation.missingHoldSeconds = m_channelMissingHold->value();
    c.channelAggregation.historySeconds = m_channelHistorySeconds->value();
    for (int row = 0; row < m_channelPriorTable->rowCount(); ++row) {
        const auto* idItem = m_channelPriorTable->item(row, 0);
        const auto* nameItem = m_channelPriorTable->item(row, 1);
        const auto* startItem = m_channelPriorTable->item(row, 2);
        const auto* endItem = m_channelPriorTable->item(row, 3);
        const auto* enabledItem = m_channelPriorTable->item(row, 4);
        algorithm::ChannelPrior prior;
        prior.id = idItem ? idItem->data(Qt::UserRole).toLongLong() : 0;
        prior.name = nameItem ? nameItem->text().trimmed().toStdString() : std::string{};
        prior.enabled = enabledItem && enabledItem->checkState() == Qt::Checked;
        if (!startItem || !FrequencySpinBox::parseFrequencyText(startItem->text(), prior.startFrequencyHz))
            prior.startFrequencyHz = -1;
        if (!endItem || !FrequencySpinBox::parseFrequencyText(endItem->text(), prior.endFrequencyHz))
            prior.endFrequencyHz = -1;
        c.channelAggregation.priors.push_back(std::move(prior));
    }
    c.maxSignals = static_cast<std::size_t>(m_maxSignals->value());
    return c;
}

void DetectionConfigWidget::setConfig(const algorithm::DetectionConfig& c)
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
    m_fusionGap->setFrequencyHz(c.fusion.gapHz);
    m_trackOverlap->setValue(c.tracker.overlapRatio);
    m_maxMiss->setValue(c.tracker.maxMissSeconds);
    m_boundaryStability->setChecked(c.tracker.boundaryStabilityEnabled);
    m_trackMaxBandwidthRatio->setValue(c.tracker.maxBandwidthRatio);
    m_trackCenterDistanceRatio->setValue(c.tracker.centerDistanceRatio);
    m_trackMedianWindow->setValue(static_cast<int>(c.tracker.medianWindow));
    m_trackSmoothingAlpha->setValue(c.tracker.smoothingAlpha);
    m_trackJumpConfirmations->setValue(static_cast<int>(c.tracker.jumpConfirmationCount));
    m_trackJumpEdgeChangeRatio->setValue(c.tracker.jumpEdgeChangeRatio);
    m_trackJumpCenterToleranceRatio->setValue(c.tracker.jumpCenterToleranceRatio);
    m_trackJumpBandwidthToleranceRatio->setValue(c.tracker.jumpBandwidthToleranceRatio);
    m_channelAggregationEnabled->setChecked(c.channelAggregation.enabled);
    m_channelMaximumBandwidth->setFrequencyHz(c.channelAggregation.maximumAutomaticBandwidthHz);
    m_channelHighThreshold->setValue(c.channelAggregation.highThresholdDb);
    m_channelLowThreshold->setValue(c.channelAggregation.lowThresholdDb);
    m_channelMinimumSupport->setValue(c.channelAggregation.minimumSupportRatio);
    m_channelMinimumCoverage->setValue(c.channelAggregation.minimumCoverageRatio);
    m_channelMergeConfirmations->setValue(static_cast<int>(c.channelAggregation.mergeConfirmationCount));
    m_channelSplitConfirmations->setValue(static_cast<int>(c.channelAggregation.splitConfirmationCount));
    m_channelMissingConfirmations->setValue(static_cast<int>(c.channelAggregation.missingConfirmationCount));
    m_channelMissingHold->setValue(c.channelAggregation.missingHoldSeconds);
    m_channelHistorySeconds->setValue(c.channelAggregation.historySeconds);
    m_channelPriorTable->setRowCount(static_cast<int>(c.channelAggregation.priors.size()));
    for (int row = 0; row < m_channelPriorTable->rowCount(); ++row) {
        const auto& prior = c.channelAggregation.priors[static_cast<std::size_t>(row)];
        auto* id = new QTableWidgetItem;
        id->setData(Qt::UserRole, static_cast<qlonglong>(prior.id));
        auto* name = new QTableWidgetItem(QString::fromStdString(prior.name));
        auto* start = new QTableWidgetItem(prior.startFrequencyHz >= 0
            ? FrequencySpinBox::formatFrequency(prior.startFrequencyHz) : QString{});
        auto* end = new QTableWidgetItem(prior.endFrequencyHz >= 0
            ? FrequencySpinBox::formatFrequency(prior.endFrequencyHz) : QString{});
        auto* enabled = new QTableWidgetItem;
        enabled->setFlags(enabled->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        enabled->setCheckState(prior.enabled ? Qt::Checked : Qt::Unchecked);
        m_channelPriorTable->setItem(row, 0, id);
        m_channelPriorTable->setItem(row, 1, name);
        m_channelPriorTable->setItem(row, 2, start);
        m_channelPriorTable->setItem(row, 3, end);
        m_channelPriorTable->setItem(row, 4, enabled);
    }
    m_maxSignals->setValue(static_cast<int>(c.maxSignals));
}

void DetectionConfigWidget::setFeedback(const QString& message)
{
    m_feedback->setText(message);
}

void DetectionConfigWidget::addChannelPrior()
{
    std::int64_t nextId = 1;
    for (int row = 0; row < m_channelPriorTable->rowCount(); ++row) {
        const auto* item = m_channelPriorTable->item(row, 0);
        if (item) nextId = std::max(nextId, item->data(Qt::UserRole).toLongLong() + 1);
    }
    const int row = m_channelPriorTable->rowCount();
    m_channelPriorTable->insertRow(row);
    auto* id = new QTableWidgetItem;
    id->setData(Qt::UserRole, static_cast<qlonglong>(nextId));
    m_channelPriorTable->setItem(row, 0, id);
    m_channelPriorTable->setItem(row, 1, new QTableWidgetItem(QStringLiteral("信道 %1").arg(nextId)));
    m_channelPriorTable->setItem(row, 2, new QTableWidgetItem);
    m_channelPriorTable->setItem(row, 3, new QTableWidgetItem);
    auto* enabled = new QTableWidgetItem;
    enabled->setFlags(enabled->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    enabled->setCheckState(Qt::Checked);
    m_channelPriorTable->setItem(row, 4, enabled);
    m_channelPriorTable->selectRow(row);
}

void DetectionConfigWidget::removeChannelPrior()
{
    const int row = m_channelPriorTable->currentRow();
    if (row >= 0) m_channelPriorTable->removeRow(row);
}

} // namespace scn::app
