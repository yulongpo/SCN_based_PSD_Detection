#include "CollMonitor.h"
#include "comm/CommonMacros.h"
#include "MonitorUiConfig.h"
#include "comm/ScreenScale.h"
#include "comm/ThemeManager.h"
#include "tfwaterfall/HQTfwaterfall.h"
#include "spectrum/HQSpectrum.h"
#include "splitter/HQSplitter.h"
#include "listbox/HQListBox.h"
#include <QFrame>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>
#include <cstring>

#include "spdlog/fmt/bundled/base.h"

namespace
{
bool isProvisionalDetection(HQSigMF* signal)
{
    if (!signal) return false;

    auto section = signal->getSection(
        Domain::FEATURE,
        static_cast<int32_t>(FeatureCategory::DETECTION_PROGRESS));
    if (!section || section->property.data_type != DataType::OBJ) return false;

    const auto progress = section->getData<HQSigMF::DetectionProgress>();
    return progress.first && progress.second > 0 &&
        progress.first[0].stage == DetectionStage::PROVISIONAL;
}
}

CollMonitor::CollMonitor(QWidget *parent)
    : QWidget(parent), m_updateData(true)
{
    setAttribute(Qt::WA_StyledBackground, true);

    initUI();
    const MonitorUiConfig uiConfig = MonitorUiConfig::load(
        MonitorUiConfig::defaultPath());
    m_waterfall->setMarksVisible(uiConfig.waterfallShowDetectionResults);
    initConnections();
    initDataProcessing();
}

void CollMonitor::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

    const qreal w = width();
    const qreal h = height();

    // 圆角半径 16px、边框宽度 1px（设计稿 2x 基准）
    const qreal radius  = DPR_REAL(16.0 * scale, dpr);
    const qreal borderW = DPR_REAL(1.0 * scale, dpr);
    const qreal halfBW  = borderW * 0.5;

    // --- 绘制圆角边框（颜色从主题配置读取，矩形向内偏移 pad）---
    QPainterPath borderPath;
    borderPath.addRoundedRect(
        QRectF(halfBW, halfBW,
               w - borderW, h - borderW),
        radius - halfBW, radius - halfBW);

    painter.setPen(QPen(ThemeManager::instance().color("CollMonitor.borderColor"), borderW));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(borderPath);
}

void CollMonitor::initUI()
{
    const int padInt = 0;

    auto contentLayout = new QVBoxLayout(this);
    // 设置内边距，使子控件不会贴到自绘边框的边缘
    contentLayout->setContentsMargins(padInt, padInt, padInt, padInt);

    // --- 创建时频瀑布图 ---
    m_waterfall = new HQTfwaterfall(this);

    // --- 创建频谱图 ---
    m_spectrum = new HQSpectrum(this);

    // --- 垂直分裂器：时频图在上、频谱图在下，可拖动调整两者高度 ---
    // 分隔线默认灰色细线，鼠标悬停时变亮蓝粗线（仅视觉加粗，不影响两侧 widget 尺寸）
    m_splitter = new HQSplitter(this);
    m_splitter->addWidget(m_waterfall);
    m_splitter->addWidget(m_spectrum);
    m_splitter->setStretchFactor(0, 1);     // 时频图 : 频谱图 = 1 : 2（与原先布局比例一致）
    m_splitter->setStretchFactor(1, 2);
    contentLayout->addWidget(m_splitter, 3);    // 3 = 时频图(1) + 频谱图(2)，保持与列表区(1)的相对占比

    auto createLine = [this]()-> QFrame*
    {
        auto separator = new QFrame(this);
        separator->setFrameShape(QFrame::HLine);
        separator->setFrameShadow(QFrame::Plain);
        separator->setFixedHeight(1);
        separator->setStyleSheet(
            QString("QFrame { border: none; background-color: %1; }")
                .arg(ThemeManager::instance().colorString("CollMonitor.borderColor")));
        return separator;
    };

    // --- 分隔线（颜色从主题配置读取）---
    m_separatorList = createLine();
    contentLayout->addWidget(m_separatorList);

    // --- 创建信号列表框 ---
    m_listBox = new HQListBox(this);
    contentLayout->addWidget(m_listBox, 1);

    m_waterfall->setResolution(m_freqBins, 100, QCPRange(m_freqStart, m_freqEnd), QCPRange(-25, 0));
    m_waterfall->setXRangeLimit(m_freqStart, m_freqEnd);
    m_waterfall->setYRangeLimit(-25, 0);
    m_spectrum->setResolution(m_freqBins, QCPRange(m_freqStart, m_freqEnd));
    m_spectrum->setXRangeLimit(m_freqStart, m_freqEnd);

    // Worker 完成重算后，直接推送数据到频谱图刷新显示
    connect(m_waterfall, &HQTfwaterfall::maxHoldAvgUpdated,
            m_spectrum, &HQSpectrum::onMaxHoldAvgUpdated);
}

void CollMonitor::initConnections()
{
    // ---- X 轴范围双向同步 ----
    using RangeSig = void(QCPAxis::*)(const QCPRange &);

    // 频谱图 x 轴变化 → 同步到时频图
    connect(m_spectrum->xAxis, static_cast<RangeSig>(&QCPAxis::rangeChanged),
            this, [this](const QCPRange &r) {
        if (m_xRangeSyncing) return;
        m_xRangeSyncing = true;
        m_waterfall->setXRange(r.lower, r.upper);
        m_xRangeSyncing = false;
    });
    // 时频图 x 轴变化 → 同步到频谱图
    connect(m_waterfall->xAxis, static_cast<RangeSig>(&QCPAxis::rangeChanged),
            this, [this](const QCPRange &r) {
        if (m_xRangeSyncing) return;
        m_xRangeSyncing = true;
        m_spectrum->setXRange(r.lower, r.upper);
        m_xRangeSyncing = false;
    });
    // 列表框
    connect(m_listBox, &HQListBox::addSignalNum, this, &CollMonitor::addSignalNum);
    // 列表框行双击 → 转发给上层（供打开 RecordDialog 等操作）
    connect(m_listBox, &HQListBox::rowDoubleClicked, this, &CollMonitor::signalRowDoubleClicked);

    // 时频图最后一帧时间 → 列表行过期清理
    connect(m_waterfall, &HQTfwaterfall::lastFrameTimeUpdated,
            m_listBox, &HQListBox::removeOldRows);
    // 列表行单击 → 频谱图定位/高亮对应标记框
    connect(m_listBox, &HQListBox::rowClickedForMark,
            m_spectrum, &HQSpectrum::onRowSelectedForMark);
    // 列表行单击 → 时频图定位/高亮对应标记框
    connect(m_listBox, &HQListBox::rowClickedForMark,
            m_waterfall, &HQTfwaterfall::onRowSelectedForMark);

    // 频谱图标记框点击 → 列表自动滚动到对应行（反向同步）
    connect(m_spectrum, &HQSpectrum::markClicked,
            m_listBox, &HQListBox::scrollToRowById);

    // 时频图标记框点击 → 列表自动滚动到对应行（反向同步）
    connect(m_waterfall, &HQTfwaterfall::markClicked,
            m_listBox, &HQListBox::scrollToRowById);

    // 频谱图选中变化 → 时频图同步选中（双向，onExternalMarkSelected 不再反向发信号，无循环）
    connect(m_spectrum, &HQSpectrum::markSelectionChanged,
            m_waterfall, &HQTfwaterfall::onExternalMarkSelected);
    connect(m_waterfall, &HQTfwaterfall::markSelectionChanged,
            m_spectrum, &HQSpectrum::onExternalMarkSelected);
}

void CollMonitor::initDataProcessing()
{
    // 启动 ~30fps 定时器，从 HQSigMF 队列中取出数据并驱动显示
    m_processTimer = new QTimer(this);
    m_processTimer->setTimerType(Qt::PreciseTimer);
    connect(m_processTimer, &QTimer::timeout,
            this, &CollMonitor::onProcessTick);
    m_processTimer->start(33);  // ~30fps（1000ms / 30 ≈ 33.33ms）
}

void CollMonitor::getMinMax(float* data, int startIndex, int endIndex, double& min, double& max)
{
    // 首先，找到10个最小值（如果数据少于10个，则取所有值）
    int dataSize = endIndex - startIndex;
    if (dataSize <= 0) {
        min = 0.0;
        max = 80.0; // 如果没有数据，使用默认值
        return;
    }

    // 确定要考虑的值数量（10个或可用数据大小中的较小值）
    int numValuesToConsider = std::min(10, dataSize);

    // 创建向量来存储最小值
    std::vector<float> smallestValues;
    smallestValues.reserve(numValuesToConsider);

    // 用前numValuesToConsider个值初始化
    for (int i = startIndex; i < startIndex + numValuesToConsider; i++) {
        smallestValues.push_back(data[i]);
    }

    // 按升序排序初始值，使最大值在末尾
    std::sort(smallestValues.begin(), smallestValues.end());

    // 处理剩余的值
    for (int i = startIndex + numValuesToConsider; i < endIndex; i++) {
        float currentValue = data[i];

        // 如果当前值小于我们最小值列表中的最大值
        if (currentValue < smallestValues.back()) {
            // 用当前值替换最大值
            smallestValues.back() = currentValue;

            // 重新排序以保持升序
            std::sort(smallestValues.begin(), smallestValues.end());
        }
    }

    // 计算最小值的平均值
    double sum = 0.0;
    for (float val : smallestValues) {
        sum += val;
    }
    double average = sum / smallestValues.size();

    // 根据要求设置最小值和最大值
    min = average;
    max = min + 60.0;
}

void CollMonitor::enqueueIcdData(QSharedPointer<HQSigMF> data)
{
    if (!m_updateData.load() || !data) return;
    QMutexLocker locker(&m_queueMutex);
    m_sigQueue.enqueue(data);
}

void CollMonitor::slotParams(int64_t fc, int64_t bw, int64_t refLevel)
{
    const int64_t nextFreqStart = fc - bw / 2;
    const int64_t nextFreqEnd = fc + bw / 2;
    const bool geometryChanged = nextFreqStart != m_freqStart ||
                                 nextFreqEnd != m_freqEnd;
    m_freqStart = nextFreqStart;
    m_freqEnd = nextFreqEnd;
    m_refLevel = refLevel;
    m_waterfall->setXRangeLimit(m_freqStart, m_freqEnd);
    m_waterfall->setResolution(m_freqBins, 100, QCPRange(m_freqStart, m_freqEnd), QCPRange(-25, 0));
    m_waterfall->setYRangeLimit(-25, 0);
    m_spectrum->setXRangeLimit(m_freqStart, m_freqEnd);
    m_spectrum->setResolution(m_freqBins, QCPRange(m_freqStart, m_freqEnd));

    // 限制频谱图Y轴范围
    m_spectrum->setYRangeLimit(m_refLevel - 100.0f, m_refLevel);
    m_spectrum->setYRange(m_refLevel - 100.0f, m_refLevel);
    // 设置时频图色阶
    m_waterfall->setColorDataRange(m_refLevel - 100.0f, m_refLevel);

    // Only tuning/viewport geometry changes invalidate track coordinates. A
    // repeated parameter notification must not erase active or historical marks.
    if (geometryChanged)
    {
        m_detectionOverlay.clear();
        m_spectrum->clearMarks();
        m_waterfall->clearMarks();
    }
}

void CollMonitor::slotIsUpdateData(bool flag)
{
    // false 代表点击了暂停
    m_updateData.store(flag);
}

void CollMonitor::slotClearData()
{
    {
        QMutexLocker locker(&m_queueMutex);
        m_sigQueue.clear();
    }
    m_currentSig.reset();
    m_currentFrameIdx = 0;
    m_framesInCurrent = 0;
    m_listBox->clear();
    m_spectrum->clearData();
    m_waterfall->clearData();
    m_detectionOverlay.clear();
    m_overlayClock.invalidate();
    m_firstFrame = true;
    resetDetectionProgress();
}

void CollMonitor::updateDetectionProgress(const QSharedPointer<HQSigMF> &signal)
{
    if (!signal) return;

    auto section = signal->getSection(
        Domain::FEATURE,
        static_cast<int32_t>(FeatureCategory::DETECTION_PROGRESS));
    if (!section || section->property.data_type != DataType::OBJ) return;

    const auto data = section->getData<HQSigMF::DetectionProgress>();
    if (!data.first || data.second == 0) return;

    const HQSigMF::DetectionProgress &progress = data.first[0];
    const int accumulated = qMax(0, static_cast<int>(progress.accumulated_frames));
    const int minOutput = qMax(0, static_cast<int>(progress.min_output_frames));
    const int required = qMax(0, static_cast<int>(progress.required_frames));
    emit detectionProgressChanged(static_cast<int>(progress.stage), accumulated,
                                  minOutput, required);
}

void CollMonitor::resetDetectionProgress()
{
    emit detectionProgressCleared();
}

void CollMonitor::renderDetectionOverlay()
{
    m_spectrum->clearMarks();
    const auto snapshots = m_detectionOverlay.snapshots();
    for (const auto& snapshot : snapshots)
    {
        HQSigMF::DetectionObject object = snapshot.object;
        m_spectrum->addObj(&object, snapshot.opacity);
    }
}

void CollMonitor::slotListDbRecord(bool flag)
{
    // flag = true：允许列表框双击回放，频谱图和时频图可交互操作
    // flag = false：禁止列表框双击回放，频谱图和时频图不可操作（冻结视图，防止误触）
    m_listBox->setDbRecord(flag);
    // m_spectrum->setOperatorEnabled(flag);   // 控制频谱图可交互操作（拖拽缩放、标记点击、工具栏按钮等）
    // m_waterfall->setOperatorEnabled(flag);  // 控制时频图可交互操作（滚轮缩放、拖拽等）
}

void CollMonitor::onProcessTick()
{
    // UI 过渡独立于数据包到达：即使队列暂时为空，边界仍平滑收敛或淡出。
    double elapsedSeconds = 0.033;
    if (m_overlayClock.isValid())
        elapsedSeconds = qMax(0.001, m_overlayClock.restart() / 1000.0);
    else
        m_overlayClock.start();
    const auto timedOutOverlayIds = m_detectionOverlay.advance(elapsedSeconds);
    for (const int64_t trackId : timedOutOverlayIds)
        m_waterfall->finishObj(trackId);
    renderDetectionOverlay();

    // ---- 检查是否需要切换到下一个 HQSigMF ----
    if (!m_currentSig || m_currentFrameIdx >= m_framesInCurrent)
    {
        QSharedPointer<HQSigMF> nextSig;
        {
            QMutexLocker locker(&m_queueMutex);
            if (m_sigQueue.isEmpty()) return;
            nextSig = m_sigQueue.dequeue();
        }

        updateDetectionProgress(nextSig);

        // 实时显示优先使用当前 SHORT_TERM；老数据仅有 LONG_TERM 时兼容回退。
        auto sections = nextSig->getSections(Domain::FREQUENCY);
        if (sections.empty()) return;

        std::shared_ptr<HQSigMF::Section> liveSection;
        for (const auto& section : sections)
        {
            if (section && section->property.category ==
                static_cast<int32_t>(FrequencyCategory::SHORT_TERM))
            {
                liveSection = section;
                break;
            }
        }
        if (!liveSection)
        {
            for (const auto& section : sections)
            {
                if (section && section->property.category ==
                    static_cast<int32_t>(FrequencyCategory::LONG_TERM))
                {
                    liveSection = section;
                    break;
                }
            }
        }
        if (!liveSection) return;

        const auto liveData = liveSection->getData<float>();
        const float* dataPtr = liveData.first;
        const size_t dataCnt = liveData.second;
        const int freqBins = liveSection->property.frequency_num;
        if (!dataPtr || dataCnt == 0 || freqBins <= 0) return;

        if (m_freqBins != freqBins)
        {
            m_waterfall->setXRangeLimit(m_freqStart, m_freqEnd);
            m_waterfall->setResolution(freqBins, 100, QCPRange(m_freqStart, m_freqEnd), QCPRange(-25, 0));
            m_waterfall->setYRangeLimit(-25, 0);
            m_spectrum->setXRangeLimit(m_freqStart, m_freqEnd);
            m_spectrum->setResolution(freqBins, QCPRange(m_freqStart, m_freqEnd));
            m_freqBins = freqBins;
        }

        m_currentSig = nextSig;
        m_framesInCurrent = static_cast<int>(dataCnt / static_cast<size_t>(freqBins));
        m_currentFrameIdx = 0;
        if (m_framesInCurrent <= 0) { m_currentSig.reset(); return; }

        if (m_firstFrame)
        {
            m_firstFrame = false;
            double min = 10000.0, max = -10000.0;
            std::vector<float> averaged(static_cast<size_t>(m_freqBins), 0.0f);
            for (int frame = 0; frame < m_framesInCurrent; ++frame)
                for (int bin = 0; bin < m_freqBins; ++bin)
                    averaged[static_cast<size_t>(bin)] +=
                        dataPtr[static_cast<size_t>(frame) * m_freqBins + bin];
            for (float& value : averaged) value /= m_framesInCurrent;
            getMinMax(averaged.data(), 0, m_freqBins, min, max);
            m_waterfall->setColorDataRange(min, max);
        }
    }

    if (!m_currentSig) return;

    // ---- 获取当前帧数据(默认下标0当做一帧处理) ----
    auto sections = m_currentSig->getSections(Domain::FREQUENCY);
    std::shared_ptr<HQSigMF::Section> liveSection;
    for (const auto& section : sections)
    {
        if (section && section->property.category ==
            static_cast<int32_t>(FrequencyCategory::SHORT_TERM))
        {
            liveSection = section;
            break;
        }
    }
    if (!liveSection)
    {
        for (const auto& section : sections)
        {
            if (section && section->property.category ==
                static_cast<int32_t>(FrequencyCategory::LONG_TERM))
            {
                liveSection = section;
                break;
            }
        }
    }
    if (!liveSection) { m_currentSig.reset(); return; }

    auto dataPair = liveSection->getData<float>();
    const float *dataPtr = dataPair.first;
    size_t       dataCnt = dataPair.second;
    if (!dataPtr || dataCnt <= 0) { m_currentSig.reset(); return; }

    // ---- 取出当前帧数据并推送 ----
    const float *frameData = dataPtr + m_currentFrameIdx * m_freqBins;

    // ---- 获取检测标记 ----
    if (m_currentFrameIdx == 0) // 每包的无论多少帧，检测标记处理一次
    {
        const bool provisional = isProvisionalDetection(m_currentSig.data());
        const auto ingestResult = m_detectionOverlay.ingest(
            *m_currentSig, provisional,
            static_cast<double>(m_freqEnd - m_freqStart));
        // Tracker 的长时间身份保留仅用于后续同 ID 重连。收到失活事件时，
        // 时频图立即冻结当前活动段，避免标记随 60 s 跟踪 TTL 驻留。
        for (const int64_t trackId : ingestResult.inactiveTrackIds)
            m_waterfall->finishObj(trackId);

        for (const auto& observation : ingestResult.observations)
        {
            HQSigMF::DetectionObject object = observation.object;
            // 仅确认轨迹进入正式列表；暂定轨迹仍可在两张实时图中观察。
            if (!observation.provisional &&
                observation.state == TrackLifeState::CONFIRMED)
                m_listBox->addRow(&object);
            m_waterfall->addObj(&object);
        }
        renderDetectionOverlay();
    }

    m_spectrum->post(frameData);
    m_waterfall->post(m_currentSig->config().physical_timestamp_ns / 1e6, frameData);

    m_currentFrameIdx++;
	if (m_currentFrameIdx >= m_framesInCurrent)
	{
		m_currentSig.reset();
	}
}
