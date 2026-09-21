#include "HQTfwaterfall.h"
#include "../plot/qcustomplot.h"
#include "comm/CommonMacros.h"
#include "comm/ScreenScale.h"
#include "comm/ThemeManager.h"

#include <QResizeEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QDateTime>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <algorithm>
#include <limits>

// ============================================================================
// WaterfallTimeTicker::getTickLabel
// ============================================================================

QString WaterfallTimeTicker::getTickLabel(double tick, const QLocale &locale,
                                           QChar formatChar, int precision)
{
    if (!m_timeBuf || m_timeBuf->empty() || m_showSize <= 0 || m_timeRange.size() <= 0.0) {
        return QCPAxisTicker::getTickLabel(tick, locale, formatChar, precision);
    }

    const double frac = (tick - m_timeRange.lower) / m_timeRange.size();
    const int displayRow = qBound(0,
        static_cast<int>(frac * (m_showSize - 1) + 0.5),
        m_showSize - 1);
    const int circRow = (m_oldestRow + displayRow) % m_showSize;

    return HQTfwaterfall::formatTimestamp((*m_timeBuf)[circRow]);
}

// ============================================================================
// MaxSpectrumWorker 实现
// ============================================================================

MaxSpectrumWorker::MaxSpectrumWorker(QObject *parent)
    : QObject(parent)
{
}

void MaxSpectrumWorker::init(int fftlen, int showSize, const float *circBuf, QMutex *circBufMutex,
                             const int64_t *frameCntPtr, std::atomic<int64_t> *workerProcessedFramePtr)
{
    m_fftlen                    = fftlen;
    m_showSize                  = showSize;
    m_circBuf                   = circBuf;
    m_circBufMutex              = circBufMutex;
    m_framePostedCountPtr       = frameCntPtr;
    m_workerProcessedFramePtr   = workerProcessedFramePtr;

    m_numChunks = (fftlen + CHUNK_SIZE - 1) / CHUNK_SIZE;

    const float negInf = -std::numeric_limits<float>::infinity();
    m_localMax.assign(fftlen, negInf);
    m_maxHold.assign(fftlen, -200.0f);
    m_avgSum.assign(fftlen, 0.0f);
    m_avgData.assign(fftlen, 0.0f);
    m_avgCount = 0;
    m_lastProcessedFrame = -1;

    {
        QMutexLocker lock(&m_resultMutex);
        m_sharedMax.assign(fftlen, negInf);
        m_sharedMaxHold.assign(fftlen, -200.0f);
        m_sharedAvgData.assign(fftlen, 0.0f);
        m_processedChunks.assign(m_numChunks, 0);
    }
    {
        QMutexLocker lock(&m_snapshotMutex);
        m_dirtySnapshot.assign(m_numChunks, 0);
    }
}

void MaxSpectrumWorker::setDirtyFlags(const uint8_t *flags, int count)
{
    // UI 线程调用，将脏标记快照到 Worker 内部（互斥锁保护）
    QMutexLocker lock(&m_snapshotMutex);
    m_dirtySnapshot.assign(flags, flags + count);
}

void MaxSpectrumWorker::reset()
{
    const float negInf = -std::numeric_limits<float>::infinity();
    std::fill(m_localMax.begin(), m_localMax.end(), negInf);
    std::fill(m_maxHold.begin(), m_maxHold.end(), -200.0f);
    std::fill(m_avgSum.begin(), m_avgSum.end(), 0.0f);
    std::fill(m_avgData.begin(), m_avgData.end(), 0.0f);
    m_avgCount = 0;
    m_lastProcessedFrame = -1;

    QMutexLocker lock(&m_resultMutex);
    std::fill(m_sharedMax.begin(), m_sharedMax.end(), negInf);
    std::fill(m_sharedMaxHold.begin(), m_sharedMaxHold.end(), -200.0f);
    std::fill(m_sharedAvgData.begin(), m_sharedAvgData.end(), 0.0f);
    std::fill(m_processedChunks.begin(), m_processedChunks.end(), 0);

    QMutexLocker snapshotLock(&m_snapshotMutex);
    std::fill(m_dirtySnapshot.begin(), m_dirtySnapshot.end(), 0);
}

std::vector<float> MaxSpectrumWorker::getSharedMax() const
{
    QMutexLocker lock(&m_resultMutex);
    return m_sharedMax;  // 拷贝返回，避免调用方持有锁期间访问
}

std::vector<uint8_t> MaxSpectrumWorker::getProcessedChunks() const
{
    QMutexLocker lock(&m_resultMutex);
    return m_processedChunks;
}

std::vector<float> MaxSpectrumWorker::getSharedMaxHold() const
{
    QMutexLocker lock(&m_resultMutex);
    return m_sharedMaxHold;
}

std::vector<float> MaxSpectrumWorker::getSharedAvgData() const
{
    QMutexLocker lock(&m_resultMutex);
    return m_sharedAvgData;
}

void MaxSpectrumWorker::recomputeAsync()
{
    // ---- 本方法在后台线程中执行 ----

    if (!m_circBuf || m_fftlen <= 0 || m_showSize <= 0) return;

    const int nx        = m_fftlen;
    const int ny        = m_showSize;
    const int chunkSize = CHUNK_SIZE;
    const int numChunks = m_numChunks;

    // ========================================================================
    // 步骤0：处理未处理帧 → 更新最大保持 + 运行平均，同时更新原子计数器
    //   UI 线程通过 m_workerProcessedFrame 检查落后程度，超过缓冲区大小时
    //   阻塞等待，保证循环缓冲区中的数据不被覆盖
    // ========================================================================
    if (m_framePostedCountPtr) {
        const int64_t totalPosted = *m_framePostedCountPtr;
        const int64_t startFrame  = m_lastProcessedFrame + 1;
        const int64_t endFrame    = totalPosted - 1;

        if (startFrame <= endFrame) {
            for (int64_t f = startFrame; f <= endFrame; ++f) {
                const int row = static_cast<int>(f % ny);
                {
                    QMutexLocker circLock(m_circBufMutex);
                    const float *spectrum = m_circBuf + row * nx;
                    for (int i = 0; i < nx; ++i) {
                        if (spectrum[i] > m_maxHold[i]) m_maxHold[i] = spectrum[i];
                        m_avgSum[i] += spectrum[i];
                    }
                }
                m_avgCount++;
            }
            if (m_avgCount > 0) {
                const float invCount = 1.0f / static_cast<float>(m_avgCount);
                for (int i = 0; i < nx; ++i) m_avgData[i] = m_avgSum[i] * invCount;
            }
        }
        m_lastProcessedFrame = endFrame;

        // 更新原子计数器 → UI 线程可安全写入新帧而不会覆盖未处理数据
        if (m_workerProcessedFramePtr) {
            m_workerProcessedFramePtr->store(m_lastProcessedFrame, std::memory_order_release);
        }
    }

    // ========================================================================
    // 步骤1：从快照缓冲区中读取当前脏标记
    // ========================================================================
    std::vector<uint8_t> dirtySnapshot;
    {
        QMutexLocker lock(&m_snapshotMutex);
        dirtySnapshot = m_dirtySnapshot;
    }

    // ========================================================================
    // 步骤2：将要重算的 chunk 重置为 -inf
    // ========================================================================
    const float negInf = -std::numeric_limits<float>::infinity();
    for (int c = 0; c < numChunks; ++c) {
        if (!dirtySnapshot[c]) continue;
        const int binStart = c * chunkSize;
        const int binEnd   = std::min(binStart + chunkSize, nx);
        std::fill(m_localMax.begin() + binStart, m_localMax.begin() + binEnd, negInf);
    }

    // ========================================================================
    // 步骤3：按 chunk 遍历循环缓冲区，每个 chunk 加锁一次
    // ========================================================================
    for (int c = 0; c < numChunks; ++c) {
        if (!dirtySnapshot[c]) continue;
        const int binStart = c * chunkSize;
        const int binEnd   = std::min(binStart + chunkSize, nx);
        const int binCount = binEnd - binStart;

        {
            QMutexLocker circLock(m_circBufMutex);
            for (int r = 0; r < ny; ++r) {
                const float *src = m_circBuf + r * nx + binStart;
                float *dst = m_localMax.data() + binStart;
                for (int b = 0; b < binCount; ++b) {
                    if (src[b] > dst[b]) dst[b] = src[b];
                }
            }
        }
    }

    // ========================================================================
    // 步骤4：将结果拷贝到共享缓冲区
    // ========================================================================
    {
        QMutexLocker lock(&m_resultMutex);
        std::copy(m_localMax.begin(), m_localMax.end(), m_sharedMax.begin());
        std::copy(m_maxHold.begin(), m_maxHold.end(), m_sharedMaxHold.begin());
        std::copy(m_avgData.begin(), m_avgData.end(), m_sharedAvgData.begin());
        m_processedChunks = dirtySnapshot;
    }

    // ========================================================================
    // 步骤5：通知 UI 线程合并结果
    // ========================================================================
    emit recomputeFinished();
}

// ============================================================================
// HQTfwaterfall 构造与析构
// ============================================================================

HQTfwaterfall::HQTfwaterfall(QWidget *parent)
    : SpecPlotBase(parent)
{
    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

    // ---- 预分配标记对象池（避免 addObj 时频繁申请/释放内存） ----
    // 构造 HQ_TFWATERFALL_MARK_CAPACITY 个标记对象存入 m_marks 对象池，
    // key 为固定池内索引（0..CAPACITY-1），并将全部对象指针登记到
    // m_freeMarks 空闲 map 中待用；此后对象池节点永不迁移，指针保持有效。
    for (int i = 0; i < HQ_TFWATERFALL_MARK_CAPACITY; ++i)
    {
        auto ret = m_marks.emplace(i, HQTFMark());
        m_freeMarks[i] = &ret.first->second;
        m_markPoolKeys[&ret.first->second] = i;
    }
    m_markCount = 0;

    setBackground(Qt::transparent);
    axisRect()->setBackground(Qt::transparent);

    xAxis->grid()->setVisible(false);
    yAxis->grid()->setVisible(false);

    const int leftMargin = DPR_INT(85 * scale, dpr, 0);
    axisRect()->setAutoMargins(QCP::msTop | QCP::msRight);
    axisRect()->setMargins(QMargins(leftMargin, 0, 0, 0));

    yAxis->setNumberFormat("f");
    yAxis->setNumberPrecision(1);

    // setupFixedYLabel(QStringLiteral("时间 (s)"));
    setupFixedYLabel(QStringLiteral("帧数 "));
    xAxis->setTickLabels(false);

    // ---- 隐藏 Y 轴原生刻度，时间+帧号全由 paintEvent 手绘（保证对齐） ----
    yAxis->setTickLabels(false);
    m_timeTicker = QSharedPointer<WaterfallTimeTicker>(new WaterfallTimeTicker);
    yAxis->setTicker(m_timeTicker);

    // 轴矩形背景渐变色：顶部不透明深蓝 → 底部全透明
    QLinearGradient bgGradient;
    bgGradient.setCoordinateMode(QGradient::ObjectBoundingMode);
    bgGradient.setStart(0, 0);                          // 顶部
    bgGradient.setFinalStop(0, 1);                      // 底部
    bgGradient.setColorAt(0.0, QColor(7, 33, 63)); // 顶部实色
    bgGradient.setColorAt(1.0, QColor(6, 12, 26));   // 底部透明
    axisRect()->setBackground(QBrush(bgGradient));

    applyThemeColors();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &HQTfwaterfall::applyThemeColors);

    initGradient();
    setupColorMap();

    setColorDataRange(-120.0, 0.0);
    setYRange(0, 12.0);
    setYRangeLimit(0, 12.0);

    setResolution(4096, 256, QCPRange(0, 1000.0), QCPRange(0, 12.0));

    // 右上角"标记"悬浮按钮栏
    setupToolButtons();

    connect(xAxis, QOverload<const QCPRange &>::of(&QCPAxis::rangeChanged),
            this, [this]() { renderWaterfall(); });

    // Y 轴拖拽时触发重绘（帧号标签跟随）
    connect(yAxis, QOverload<const QCPRange &>::of(&QCPAxis::rangeChanged),
            this, QOverload<>::of(&QWidget::update));

    // ========================================================================
    // 创建最大谱重算后台线程
    // ========================================================================
    m_maxThread = new QThread(this);
    m_maxWorker = new MaxSpectrumWorker();  // 无 parent，将被 moveToThread
    m_maxWorker->moveToThread(m_maxThread);

    // Worker 完成重算后，在 UI 线程合并结果（QueuedConnection 自动跨线程）
    connect(m_maxWorker, &MaxSpectrumWorker::recomputeFinished,
            this, &HQTfwaterfall::onRecomputeFinished,
            Qt::QueuedConnection);

    // 线程结束时自动清理 Worker
    connect(m_maxThread, &QThread::finished,
            m_maxWorker, &QObject::deleteLater);

    m_maxThread->start();
}

HQTfwaterfall::~HQTfwaterfall()
{
    // 停止后台线程
    m_maxThread->quit();
    m_maxThread->wait();
}

// ========================================================================
// 时频图界面可操作性控制
// ========================================================================

void HQTfwaterfall::setOperatorEnabled(bool enable)
{
    // 利用 QWidget::setEnabled 阻止所有鼠标/滚轮事件送达控件，
    // 从而禁用时频图的拖拽、缩放、标记点击等所有交互操作。
    // 视图内容保持可见，仅交互被冻结。
    QWidget::setEnabled(enable);
}

// ============================================================================
// 色阶与颜色映射
// ============================================================================

void HQTfwaterfall::initGradient()
{
    m_gradient.clearColorStops();

    // NaN 值直接透明，不渲染任何颜色（比 nhNanColor + 透明色更可靠）
    m_gradient.setNanHandling(QCPColorGradient::nhTransparent);

    m_gradient.setColorStopAt(0.00, QColor(0,   37,   94));
    m_gradient.setColorStopAt(0.14, QColor(0,   58,  135));
    m_gradient.setColorStopAt(0.29, QColor(0,   90,  164));
    m_gradient.setColorStopAt(0.43, QColor(20, 210,  215));
    m_gradient.setColorStopAt(0.57, QColor(0,  164,  205));
    m_gradient.setColorStopAt(0.71, QColor(248, 206,   51));
    m_gradient.setColorStopAt(0.86, QColor(254, 157,   44));
    m_gradient.setColorStopAt(1.00, QColor(254, 104,   41));
}

void HQTfwaterfall::setupColorMap()
{
    m_colorMap = new QCPColorMap(xAxis, yAxis);
    m_colorMap->setGradient(m_gradient);
    m_colorMap->setInterpolate(false);
    m_colorMap->setTightBoundary(false);
}

// ============================================================================
// 数据设置
// ============================================================================

void HQTfwaterfall::setResolution(int fftlen, int showSize,
                                   const QCPRange &freqRange, const QCPRange &timeRange)
{
    m_fftlen    = fftlen;
    m_showSize  = showSize;
    m_writeRow  = 0;
    m_dispCols  = 0;
    m_freqRange = freqRange;
    m_timeRange = timeRange;

    const float nanFloat = std::numeric_limits<float>::quiet_NaN();
    m_circularBuf.assign(fftlen * showSize, nanFloat);
    m_timeBuf.assign(showSize, 0);

    // UI 侧最大谱缓冲区
    const float negInf = -std::numeric_limits<float>::infinity();
    m_maxSpectrum.assign(fftlen, negInf);

    // UI 侧脏标记数组
    const int numChunks = (fftlen + MAX_SPECTRUM_CHUNK_SIZE - 1) / MAX_SPECTRUM_CHUNK_SIZE;
    m_maxChunkDirty.assign(numChunks, 0);

    // 重置帧计数
    m_framePostedCount = 0;
    m_workerProcessedFrame.store(-1, std::memory_order_release);

    // 初始化后台 Worker
    if (m_maxWorker) {
        m_maxWorker->init(fftlen, showSize, m_circularBuf.data(), &m_circBufMutex,
                          &m_framePostedCount, &m_workerProcessedFrame);
    }

    QCPColorMapData *mapData = m_colorMap->data();

    // 若直接使用全量 fftlen（如 1228800），QCPColorMapData::setSize() 会一次性
    // 分配 fftlen × showSize × sizeof(float) ≈ 2.5 GB 内存，极易失败或造成系统卡顿。
    // 实际渲染时 renderWaterfall() 已按屏幕像素抽稀（10 倍超采样），因此初始化只需
    // 分配合理的显示列数，renderWaterfall() 会在首次渲染时自动调整为正确尺寸。
    int initCols = fftlen;
    if (m_enableDecimation) {
        // 10 倍超采样估算（与 renderWaterfall 中 pxW 计算逻辑保持一致）
        // 取当前控件宽度，最小 100px 兜底（控件可能尚未布局）
        const int pxW = qMax(width(), 100) * 10;
        initCols = qMin(fftlen, pxW);
    }
    mapData->setSize(initCols, showSize);
    mapData->setRange(freqRange, timeRange);

    // QCPColorMapData::setSize() 内部调用 fill(0) 将所有单元格初始化为 0.0，
    // 而 0.0 映射到颜色梯度顶端（橙色），在未收到频谱数据前会显示异常橙色背景。
    // 此外，QCPColorMapData::fill(double) 使用 memset 实现，无法正确填充 float NaN
    // （memset 按字节操作，NaN 的多字节位模式被截断为 unsigned char），
    // 因此必须用 std::fill_n 直接操作底层 float 数组，确保未收到数据时渲染为透明。
    const float nanVal = std::numeric_limits<float>::quiet_NaN();
    std::fill_n(mapData->mapData(), static_cast<std::size_t>(initCols) * showSize, nanVal);

    setXRange(freqRange.lower, freqRange.upper);
    setYRange(timeRange.lower, timeRange.upper);

    // 强制触发一次渲染刷新。
    // 注意：不能依赖 setXRange → QCPAxis::rangeChanged → renderWaterfall 的信号链路，
    // 因为 QCPAxis::setRange() 在范围值未变化时会直接返回，不发出 rangeChanged 信号。
    // 若调用方传入与当前相同的 freqRange，renderWaterfall() 将不会被触发，
    // 导致 mapData 停留在全 0.0 的状态，时频图显示异常橙色背景。
    renderWaterfall();
}

void HQTfwaterfall::post(const int64_t time, const float *spectrum)
{
    if (!m_colorMap || !m_colorMap->data() || !spectrum) return;
    if (m_circularBuf.empty()) return;

    const int nx = m_fftlen;
    const int ny = m_showSize;

    // ========================================================================
    // 步骤1：检测是否即将覆盖旧帧（循环缓冲区已满），标记脏 chunk（UI 线程，快速）
    // ========================================================================
    const bool isOverwriting = (m_framePostedCount >= ny);
    if (isOverwriting) {
        // m_writeRow 指向即将被覆盖的最旧帧
        const float *oldFrame = m_circularBuf.data() + m_writeRow * nx;
        handleMaxDiscard(oldFrame);
    }

    // ========================================================================
    // 步骤2：用新帧更新滑动窗口最大谱（UI 线程，~1ms）
    // ========================================================================
    updateMaxSpectrum(spectrum);

    // ========================================================================
    // 步骤3：触发后台线程（处理最大保持+平均+脏chunk重算）
    // ========================================================================
    {
        const int numChunks = static_cast<int>(m_maxChunkDirty.size());
        // 总是快照脏标记（即使没有脏 chunk，Worker 也需要处理最大保持+平均）
        m_maxWorker->setDirtyFlags(m_maxChunkDirty.data(), numChunks);

        // 原子去重：Worker 每次只处理一条信号，处理时包含所有积压帧
        bool expected = false;
        if (m_recomputePending.compare_exchange_strong(expected, true)) {
            QMetaObject::invokeMethod(m_maxWorker, "recomputeAsync", Qt::QueuedConnection);
        }
    }

    // 帧计数递增
    m_framePostedCount++;

    // ========================================================================
    // 背压检查：Worker 落后超过缓冲区容量时阻塞 UI，防止 circBuf 数据被覆盖
    //   正常情况下 Worker 处理速度远快于 UI 发送速度，此分支极少触发
    // ========================================================================
    {
        const int64_t lag = m_framePostedCount - m_workerProcessedFrame.load(std::memory_order_acquire);
        if (lag > static_cast<int64_t>(ny)) {
            // Worker 落后超过缓冲区大小，必须等待
            // 使用 processEvents 保持 UI 响应（允许重绘事件处理）
            while (m_framePostedCount - m_workerProcessedFrame.load(std::memory_order_acquire) > ny / 2)
            {
                QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 1);
            }
        }
    }

    // ========================================================================
    // 原有逻辑：写入循环缓冲区（加锁保护，避免与 Worker 线程读取竞态）
    // ========================================================================
    {
        QMutexLocker circLock(&m_circBufMutex);
        memcpy(m_circularBuf.data() + m_writeRow * nx, spectrum, nx * sizeof(float));
    }
    m_timeBuf[m_writeRow] = time;
    m_writeRow = (m_writeRow + 1) % ny;

    // 发送最后一帧的时间戳，供列表行过期清理使用
    emit lastFrameTimeUpdated(m_timeBuf[m_framePostedCount <= ny ? 0 : m_writeRow]);

    renderWaterfall();
}

// ============================================================================
// renderWaterfall
// ============================================================================

void HQTfwaterfall::renderWaterfall()
{
    QCPColorMapData *mapData = m_colorMap->data();
    const int nx = m_fftlen;
    const int ny = m_showSize;
    if (nx <= 0 || ny <= 0) return;
    float *circBuf = m_circularBuf.data();

    const int oldestRow = m_writeRow % ny;
    const QCPRange visRange = xAxis->range();
    const double freqPerBin = m_freqRange.size() / nx;
    const int visBinStart = qBound(0,
        static_cast<int>((visRange.lower - m_freqRange.lower) / freqPerBin), nx - 1);
    const int visBinEnd = qBound(visBinStart + 1,
        static_cast<int>((visRange.upper - m_freqRange.lower) / freqPerBin) + 1, nx);
    const int visBinCount = visBinEnd - visBinStart;

    // 同步刻度器状态
    if (m_timeTicker) {
        m_timeTicker->updateState(&m_timeBuf, ny, oldestRow, m_timeRange);
    }

    if (m_enableDecimation) {
        const int pxW = qMax(1, axisRect()->width() * 10);// 分辨率10倍的点
        const int colsToShow = qMin(visBinCount, pxW);

        const bool sizeChanged  = colsToShow != m_dispCols;
        const bool rangeChanged = qAbs(visRange.lower - m_lastVisLower) > 1.0 ||
                                   qAbs(visRange.upper - m_lastVisUpper) > 1.0;

        if (sizeChanged || rangeChanged) {
            if (sizeChanged) {
                mapData->setSize(colsToShow, ny);
                m_dispCols = colsToShow;
            }
            mapData->setRange(QCPRange(visRange.lower, visRange.upper), m_timeRange);
            m_lastVisLower = visRange.lower;
            m_lastVisUpper = visRange.upper;
        }

        float *mapBuf = mapData->mapData();
        if (colsToShow >= visBinCount) {
            for (int r = 0; r < ny; ++r) {
                const int srcRow = (oldestRow + r) % ny;
                memcpy(mapBuf + r * colsToShow,
                       circBuf + srcRow * nx + visBinStart,
                       visBinCount * sizeof(float));
            }
        } else {
            for (int r = 0; r < ny; ++r) {
                const int srcRow = (oldestRow + r) % ny;
                const float *src = circBuf + srcRow * nx + visBinStart;
                float *dst = mapBuf + r * colsToShow;
                for (long long c = 0; c < colsToShow; ++c)
                    dst[c] = src[(c * visBinCount) / colsToShow];
            }
        }
        mapData->fill(ny - 1, mapBuf + (ny - 1) * colsToShow, colsToShow);
    } else {
        const int colsToShow = nx;
        if (colsToShow != m_dispCols) {
            mapData->setSize(colsToShow, ny);
            mapData->setRange(m_freqRange, m_timeRange);
            m_dispCols = colsToShow;
        }
        float *mapBuf = mapData->mapData();
        if (oldestRow == 0) {
            memcpy(mapBuf, circBuf, nx * ny * sizeof(float));
        } else {
            const size_t olderCnt = (ny - oldestRow) * nx;
            const size_t newerCnt = m_writeRow * nx;
            memcpy(mapBuf,            circBuf + oldestRow * nx, olderCnt * sizeof(float));
            memcpy(mapBuf + olderCnt, circBuf,                  newerCnt * sizeof(float));
        }
        mapData->fill(ny - 1, mapBuf + (ny - 1) * nx, nx);
    }

    replot();
}

void HQTfwaterfall::setDecimationEnabled(bool enabled)
{
    m_enableDecimation = enabled;
    m_dispCols = 0;
    renderWaterfall();
}

void HQTfwaterfall::clearData()
{
    if (!m_colorMap || !m_colorMap->data()) return;
    const float nanFloat = std::numeric_limits<float>::quiet_NaN();
    std::fill(m_circularBuf.begin(), m_circularBuf.end(), nanFloat);
    std::fill(m_timeBuf.begin(), m_timeBuf.end(), 0);
    m_writeRow = 0;
    m_workerProcessedFrame.store(-1, std::memory_order_release);
    clearMarks();  // 归还全部标记到空闲池（保证对象池状态一致）

    // 重置最大谱
    resetMaxSpectrum();

    renderWaterfall();
}

void HQTfwaterfall::updateTimesAndMax(int writeRow)
{
    m_writeRow = writeRow % m_showSize;
    m_framePostedCount = writeRow;
    // 重置最大谱计算
    for (int i = 0; i < m_maxChunkDirty.size(); i++)
    {
        m_maxChunkDirty[i] = 1;
    }
    renderWaterfall();//数据可能在外部已重新设置
    QMetaObject::invokeMethod(m_maxWorker, "recomputeAsync", Qt::QueuedConnection);
}

void HQTfwaterfall::resizeEvent(QResizeEvent *event)
{
    SpecPlotBase::resizeEvent(event);
    if (!m_circularBuf.empty()) renderWaterfall();
}

void HQTfwaterfall::setColorDataRange(double min, double max)
{
    if (m_colorMap) m_colorMap->setDataRange(QCPRange(min, max));
}

void HQTfwaterfall::setInterpolate(bool enabled)
{
    if (m_colorMap) m_colorMap->setInterpolate(enabled);
}

void HQTfwaterfall::setTightBoundary(bool enabled)
{
    if (m_colorMap) m_colorMap->setTightBoundary(enabled);
}

void HQTfwaterfall::applyThemeColors()
{
    const QColor tickLabelColor = ThemeManager::instance().color(
        QStringLiteral("CollMonitor.tfwaterfall.tickLabelColor"));
    const QColor axisLabelColor = ThemeManager::instance().color(
        QStringLiteral("CollMonitor.tfwaterfall.axisLabelColor"));

    if (tickLabelColor.isValid()) setTickLabelColor(tickLabelColor);
    if (axisLabelColor.isValid()) setLabelColor(axisLabelColor);
}

// ============================================================================
// paintEvent — 在时频图左侧手绘帧序号
// ============================================================================

void HQTfwaterfall::paintEvent(QPaintEvent *event)
{
    QCustomPlot::paintEvent(event);

    if (m_timeBuf.empty() || m_showSize <= 0 || m_timeRange.size() <= 0.0) return;

    const int ny = m_showSize;
    const int oldestRow = m_writeRow % ny;
    const QCPRange visRange = yAxis->range();
    const QCPAxisRect *ar = axisRect();

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setFont(yAxis->tickLabelFont());

    QFontMetrics fm(painter.font());
    const int textH = fm.height();

    // 3 个固定帧号 + 位置比率（均基于全量程的 5%/50%/95%）
    const double ratios[3] = { 0.25, 0.5, 0.75 };
    const int frameNums[3] = {
        qBound(0, static_cast<int>(ny * 0.75), ny - 1),
        ny / 2,
        qBound(0, static_cast<int>(ny * 0.25), ny - 1)
    };

    for (int i = 0; i < 3; ++i) {
        // 标签位置跟随可见范围对应比率
        const double ratio = ratios[i];
        const double dataY = visRange.lower + ratio * visRange.size();
        const int pixelY = static_cast<int>(yAxis->coordToPixel(dataY));
        const int textY = pixelY - textH / 2 + fm.ascent() / 2;

        // 可见位置对应的数据行 → 时间
        const double frac = (dataY - m_timeRange.lower) / m_timeRange.size();
        const int displayRow = qBound(0,
            static_cast<int>(frac * (ny - 1) + 0.5), ny - 1);
        const int circRow = (oldestRow + displayRow) % ny;

        // 刻度外侧（左）：固定帧号
        painter.setPen(yAxis->tickLabelColor());
        const QString frameText = QString::number(frameNums[i]);
        painter.drawText(ar->left() - fm.horizontalAdvance(frameText) - 6, textY, frameText);

        // 刻度内侧（右）：时间
        if (m_timeBuf[circRow] != 0) painter.drawText(ar->left() + 6, textY, formatTimestamp(m_timeBuf[circRow]));
    }
    // 帧数在头尾多画两个
    int pixelY = static_cast<int>(yAxis->coordToPixel(visRange.upper));
    int textY = pixelY - textH / 2 + fm.ascent() / 2;
    const QString beginFrameText = QString::number(1);
    painter.drawText(ar->left() - fm.horizontalAdvance(beginFrameText) - 6, textY, beginFrameText);
    pixelY = static_cast<int>(yAxis->coordToPixel(visRange.lower));
    textY = pixelY - textH / 2 + fm.ascent() / 2;
    const QString endFrameText = QString::number(m_showSize);
    painter.drawText(ar->left() - fm.horizontalAdvance(endFrameText) - 6, textY, endFrameText);

    // ---- 绘制标记框 ----
    drawMarks(painter, oldestRow);
}

// ============================================================================
// formatTimestamp
// ============================================================================

QString HQTfwaterfall::formatTimestamp(int64_t timeMs)
{
    const QDateTime dateTime = QDateTime::fromMSecsSinceEpoch(timeMs);
    return dateTime.toString("HH:mm:ss");
}

// ============================================================================
// timeMsToPixelY — 时间戳 → Y 轴像素坐标映射
// ============================================================================

double HQTfwaterfall::timeMsToPixelY(int64_t targetMs) const
{
    if (m_timeBuf.empty() || m_showSize <= 0 || targetMs <= 0) return -1.0;

    int64_t oldestMs = 0;
    int64_t newestMs = 0;
    if (!visibleTimeBounds(oldestMs, newestMs) ||
        targetMs < oldestMs || targetMs > newestMs)
        return -1.0;

    const int ny        = m_showSize;
    const int oldestRow = m_writeRow % ny;

    // 步骤1：在环形缓冲区中搜索目标时间戳
    //   m_timeBuf 按时间顺序排列（从 oldestRow 开始递增），
    //   找到第一个 >= targetMs 的位置即为目标帧在缓冲区中的位置
    int displayRow = -1;  // displayRow: 0 = 最旧帧, ny-1 = 最新帧
    for (int i = 0; i < ny; ++i) {
        const int circIdx = (oldestRow + i) % ny;
        const int64_t t = m_timeBuf[circIdx];
        if (t == 0) continue;           // 跳过未初始化的帧
        if (t >= targetMs) {
            displayRow = i;
            break;
        }
    }

    if (displayRow < 0) return -1.0;    // 时间戳已滚出缓冲区

    // 步骤2：displayRow → Y 轴数据坐标 → 像素坐标
    const double frac  = static_cast<double>(displayRow) / qMax(1, ny - 1);
    const double dataY = m_timeRange.lower + frac * m_timeRange.size();

    return yAxis->coordToPixel(dataY);
}

bool HQTfwaterfall::visibleTimeBounds(int64_t& oldestMs, int64_t& newestMs) const
{
    oldestMs = 0;
    newestMs = 0;
    if (m_timeBuf.empty() || m_showSize <= 0) return false;

    const int ny = m_showSize;
    const int oldestRow = m_writeRow % ny;
    bool found = false;
    for (int i = 0; i < ny; ++i)
    {
        const int64_t timestamp = m_timeBuf[(oldestRow + i) % ny];
        if (timestamp <= 0) continue;
        if (!found)
        {
            oldestMs = timestamp;
            found = true;
        }
        newestMs = timestamp;
    }
    return found;
}

bool HQTfwaterfall::timeRangeToPixelY(int64_t startMs, int64_t stopMs,
    double& yTop, double& yBottom) const
{
    yTop = -1.0;
    yBottom = -1.0;
    if (startMs > stopMs) std::swap(startMs, stopMs);

    int64_t oldestMs = 0;
    int64_t newestMs = 0;
    if (!visibleTimeBounds(oldestMs, newestMs) ||
        stopMs < oldestMs || startMs > newestMs)
        return false;

    startMs = qMax(startMs, oldestMs);
    stopMs = qMin(stopMs, newestMs);
    yTop = timeMsToPixelY(startMs);
    yBottom = timeMsToPixelY(stopMs);
    return yTop >= 0.0 && yBottom >= 0.0;
}

// ============================================================================
// setMarksVisible — 设置标记框是否可见（与频谱图"标签"按钮联动）
// ============================================================================

void HQTfwaterfall::setMarksVisible(bool visible)
{
    const bool changed = m_marksVisible != visible;
    m_marksVisible = visible;
    if (m_btnLabel && m_btnLabel->isChecked() != visible)
    {
        const QSignalBlocker blocker(m_btnLabel);
        m_btnLabel->setChecked(visible);
    }
    updateToolButtonStyle();
    if (changed)
        replot();
}

// ============================================================================
// drawMarks — 在 paintEvent 中绘制所有标记框
// ============================================================================

void HQTfwaterfall::drawMarks(QPainter &painter, int /*oldestRow*/)
{
    // 标记框隐藏时不绘制（与频谱图"标签"按钮联动）
    if (!m_marksVisible) return;
    if (m_usedMarks.empty() && m_historicalMarks.empty()) return;

    const QCPAxisRect *ar = axisRect();

    // 历史段先绘制；每段保留当时的边界，当前带宽变化不会重写已经发生的时间。
    for (const auto& mark : m_historicalMarks)
    {
        double yTop = -1.0, yBottom = -1.0;
        if (mark.hasTimeRange())
        {
            if (!timeRangeToPixelY(mark.timeStartMs(), mark.timeStopMs(),
                                   yTop, yBottom)) continue;
        }
        mark.draw(&painter, ar, xAxis, yAxis, yTop, yBottom);
    }

    // 遍历使用中标记（map 按 id 有序，value 直接为对象指针，一次命中）
    for (const auto &kv : m_usedMarks)
    {
        HQTFMark *mark = kv.second;
        double yTop = -1.0, yBottom = -1.0;

        if (mark->hasTimeRange()) {
            // ---- 时频图模式：将时间戳转换为 Y 轴像素坐标 ----
            // 映射链路（见 timeMsToPixelY 实现）：
            //   时间戳(ms) → 搜索m_timeBuf → displayRow → axisRect几何直接算像素
            if (!timeRangeToPixelY(mark->timeStartMs(), mark->timeStopMs(),
                                   yTop, yBottom)) continue;
        }
        // 否则 yTop/yBottom 保持 -1，draw() 会绘制贯穿整个 Y 轴高度（频谱图模式）

        mark->draw(&painter, ar, xAxis, yAxis, yTop, yBottom);
    }
}

// ============================================================================
// 标记框鼠标选中交互
// ============================================================================

void HQTfwaterfall::mousePressEvent(QMouseEvent *event)
{
    // 先调用基类（处理拖拽/缩放/光标等）
    SpecPlotBase::mousePressEvent(event);

    // 左键按下时检测标记框并更新选中高亮（标记框隐藏时不响应点击）
    if (event->button() == Qt::LeftButton && m_marksVisible) {
        selectMarkAt(event->pos());
    }
}

void HQTfwaterfall::updateHoverCursor(AxisZone zone, const QPoint& mousePos)
{
    // 记录鼠标位置，供右上角"标记"按钮栏显隐判断
    m_lastMousePos = mousePos;
    updateToolBarVisibility();

    // 标记框隐藏时不响应标记悬停，光标交给基类处理
    if (!m_marksVisible) {
        SpecPlotBase::updateHoverCursor(zone, mousePos);
        return;
    }

    // 先检测是否在标记框上（无论什么区域）
    bool hoveredMark = false;
    // 先检测活动标记框。使用业务 ID map，不能假设池索引在释放后仍连续。
    for (auto it = m_usedMarks.rbegin(); it != m_usedMarks.rend(); ++it) {
        HQTFMark *mark = it->second;
        double yTop = -1.0;
        double yBottom = -1.0;
        if (!timeRangeToPixelY(mark->timeStartMs(), mark->timeStopMs(),
                               yTop, yBottom)) continue;
        if (mark->containsPoint(mousePos, axisRect(), xAxis, yTop, yBottom)) {
            hoveredMark = true;
            break;
        }
    }

    if (hoveredMark) {
        setCursor(Qt::PointingHandCursor);
        return;
    }

    // 不在标记框上，使用父类默认光标（中心区十字，拖拽区手型，外部箭头）
    SpecPlotBase::updateHoverCursor(zone, mousePos);
}

// ============================================================================
// 右上角"标记"悬浮按钮栏
// ============================================================================

void HQTfwaterfall::setupToolButtons()
{
    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

    const int btnW = DPR_INT(80 * scale, dpr, 60);
    const int btnH = DPR_INT(27 * scale, dpr, 22);

    m_buttonBar = new QFrame(this);
    m_buttonBar->setFixedHeight(btnH);
    m_toolBarHeight = btnH; // 记录按钮栏高度，供像素判断按钮栏显隐使用
    m_buttonBar->hide();
    m_buttonBar->setStyleSheet("QFrame { background: transparent; }");

    auto *layout = new QHBoxLayout(m_buttonBar);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(DPR_INT(2 * scale, dpr, 1));

    // 唯一按钮：标记（默认关闭，不显示检测标记框）
    m_btnLabel = new QPushButton(QStringLiteral("标记"), m_buttonBar);
    m_btnLabel->setFixedSize(btnW, btnH);
    m_btnLabel->setCursor(Qt::PointingHandCursor);
    m_btnLabel->setCheckable(true);
    m_btnLabel->setChecked(m_marksVisible);
    layout->addWidget(m_btnLabel);

    // 立即应用初始选中样式（避免短暂白底）
    updateToolButtonStyle();

    connect(m_btnLabel, &QPushButton::toggled,
            this, &HQTfwaterfall::onBtnLabelToggled);
}

void HQTfwaterfall::updateToolButtonStyle()
{
    if (!m_btnLabel) return;

    const bool checked = m_btnLabel->isChecked();
    const QString bgOn  = QStringLiteral("rgb(10, 140, 254)");
    const QString bgOff = QStringLiteral("rgb(7, 31, 61)");
    const QString bg    = checked ? bgOn : bgOff;

    // 边框色用网格颜色（与频谱图工具栏一致）
    const QColor gridColor = ThemeManager::instance().color(
        QStringLiteral("CollMonitor.spectrum.gridColor"));
    const QString border = gridColor.isValid()
        ? QString("rgb(%1,%2,%3)").arg(gridColor.red()).arg(gridColor.green()).arg(gridColor.blue())
        : QStringLiteral("rgb(26,48,75)");

    m_btnLabel->setStyleSheet(QString(
        "QPushButton {"
        "  background-color: %1;"
        "  color: #D6ECFF;"
        "  border: 1px solid %4;"
        "  border-radius: 6px;"
        "  font-size: 12px;"
        "}"
        "QPushButton:checked  { background-color: %2; }"
        "QPushButton:!checked { background-color: %3; }"
        "QPushButton:hover    { }"
        "QPushButton:pressed  { }"
    ).arg(bg, bgOn, bgOff, border));
}

void HQTfwaterfall::onBtnLabelToggled(bool checked)
{
    m_marksVisible = checked;
    updateToolButtonStyle();
    // 立即重绘，隐藏/显示标记框
    replot();
}

void HQTfwaterfall::updateToolBarVisibility()
{
    if (!m_buttonBar) return;

    // 按钮栏目标位置：轴矩形右上角
    QCPAxisRect *ar = axisRect();
    const int barW = m_buttonBar->sizeHint().width();
    const int gap  = DPR_INT(6 * ScreenScale::instance().scale(),
                             ScreenScale::instance().dpr(), 4);
    const int barX = static_cast<int>(ar->right()) - barW - gap;
    const int barY = static_cast<int>(ar->top()) + gap;

    // 显示区域 = m_buttonBar 控件范围，高度方向扩展为按钮高度的三倍
    const QRect showRect(barX - barW * 2, barY, barW * 3, m_toolBarHeight * 3);

    // 鼠标位于显示区域内时显示按钮栏，否则隐藏
    if (showRect.contains(m_lastMousePos)) {
        m_buttonBar->move(barX, barY);
        m_buttonBar->show();
        m_buttonBar->raise();
    } else {
        m_buttonBar->hide();
    }
}

void HQTfwaterfall::selectMarkAt(const QPoint &pos)
{
    // 遍历使用中标记（map 按 id 有序，value 直接为对象指针，一次命中）
    // 从后向前检测（逆序遍历），后添加的标记优先响应，与频谱图行为一致
    int64_t hitId = -1;
    for (auto it = m_usedMarks.rbegin(); it != m_usedMarks.rend(); ++it)
    {
        HQTFMark *mark = it->second;

        // 时频图模式：将时间戳转换为 Y 轴像素坐标（与 drawMarks 映射链路一致）
        double yTop = -1.0, yBottom = -1.0;
        if (mark->hasTimeRange()) {
            if (!timeRangeToPixelY(mark->timeStartMs(), mark->timeStopMs(),
                                   yTop, yBottom)) continue;
        }

        if (mark->containsPoint(pos, axisRect(), xAxis, yTop, yBottom)) {
            hitId = it->first;
            break;
        }
    }

    // 更新所有标记的选中状态：命中者高亮，其余取消选中
    bool changed = false;
    for (auto &kv : m_usedMarks)
    {
        HQTFMark *mark  = kv.second;
        const bool sel  = (kv.first == hitId);
        if (mark->isSelected() != sel) {
            mark->setSelected(sel);
            changed = true;
        }
    }

    // 命中标记框时，发送信号供列表反向定位同步
    if (hitId >= 0) {
        emit markClicked(hitId);
    }

    // 通知另一张图同步选中状态（-1 表示点击空白处，全部取消选中）
    emit markSelectionChanged(hitId);

    if (changed) update();
}

// ============================================================================
// 列表行单击定位 + 外部选中同步
// ============================================================================

void HQTfwaterfall::onRowSelectedForMark(int64_t id, qint64 /*freqStart*/,
                                         qint64 /*freqStop*/, int /*alarmLevel*/)
{
    // 在 m_usedMarks 中查找匹配 ID（key 即业务 id，O(log n)）
    const bool found = (m_usedMarks.find(id) != m_usedMarks.end());

    bool changed = false;
    for (auto &kv : m_usedMarks)
    {
        // 找到：仅高亮匹配标记；未找到：取消所有高亮
        const bool sel = (found && kv.first == id);
        if (kv.second->isSelected() != sel) {
            kv.second->setSelected(sel);
            changed = true;
        }
    }

    if (changed) update();
}

void HQTfwaterfall::onExternalMarkSelected(int64_t id)
{
    bool changed = false;
    for (auto &kv : m_usedMarks)
    {
        // 仅当 id 有效且与本标记匹配时选中，其余（含 -1）全部取消
        const bool sel = (id >= 0 && kv.first == id);
        if (kv.second->isSelected() != sel) {
            kv.second->setSelected(sel);
            changed = true;
        }
    }

    if (changed) update();
}

// ============================================================================
// 最大谱 — UI 线程部分（轻量操作：~1-2ms）
// ============================================================================

void HQTfwaterfall::updateMaxSpectrum(const float *spectrum)
{
    // 逐元素取最大值：m_maxSpectrum[i] = max(m_maxSpectrum[i], spectrum[i])
    // NaN 值自动被忽略（NaN > x 为 false，x > NaN 为 false）
    const int nx = m_fftlen;
    float *maxPtr = m_maxSpectrum.data();
    for (int i = 0; i < nx; ++i) {
        if (spectrum[i] > maxPtr[i]) {
            maxPtr[i] = spectrum[i];
        }
    }
}

void HQTfwaterfall::handleMaxDiscard(const float *oldFrame)
{
    // 扫描被丢弃的旧帧，标记其贡献了最大值的 chunk 为"脏"
    // 条件：oldFrame[i] >= m_maxSpectrum[i] 说明该帧曾是此频点的最大值提供者
    //
    // NaN 处理：NaN >= x 始终为 false，因此 NaN 值不会触发脏标记

    const int nx = m_fftlen;
    const int chunkSize = MAX_SPECTRUM_CHUNK_SIZE;
    const int numChunks = static_cast<int>(m_maxChunkDirty.size());
    const float *maxPtr = m_maxSpectrum.data();

    for (int c = 0; c < numChunks; ++c) {
        // 已标记的 chunk 无需重复标记
        if (m_maxChunkDirty[c]) continue;

        const int binStart = c * chunkSize;
        const int binEnd   = std::min(binStart + chunkSize, nx);

        // 扫描该 chunk 内的所有频点
        for (int i = binStart; i < binEnd; ++i) {
            if (oldFrame[i] >= maxPtr[i]) {
                m_maxChunkDirty[c] = 1;
                break;  // 只要有一个频点脏，整个 chunk 就需要重算
            }
        }
    }
}

// ============================================================================
// 最大谱 — Worker 回调（在 UI 线程中执行）
// ============================================================================

void HQTfwaterfall::onRecomputeFinished()
{
    // 本槽函数通过 QueuedConnection 在 UI 线程中执行
    // Worker 已将重算结果写入共享缓冲区，这里合并到 m_maxSpectrum

    if (!m_maxWorker) return;

    // 从 Worker 获取重算后的最大谱（线程安全拷贝）
    const std::vector<float> workerResult = m_maxWorker->getSharedMax();

    // 获取本轮 Worker 实际处理过的 chunk 列表（避免清除中间新标记的脏 chunk）
    const std::vector<uint8_t> processedChunks = m_maxWorker->getProcessedChunks();

    if (workerResult.size() != m_maxSpectrum.size()) return;

    const int chunkSize = MAX_SPECTRUM_CHUNK_SIZE;
    const int numChunks = static_cast<int>(m_maxChunkDirty.size());

    for (int c = 0; c < numChunks; ++c) {
        if (!processedChunks[c]) continue;  // 只处理 Worker 本轮实际重算过的 chunk

        const int binStart = c * chunkSize;
        const int binEnd   = std::min(binStart + chunkSize, m_fftlen);

        // 用 Worker 重算的结果覆盖 UI 侧最大谱
        std::copy(workerResult.begin() + binStart,
                  workerResult.begin() + binEnd,
                  m_maxSpectrum.begin() + binStart);

        // 只清除本轮实际处理过的脏标记
        m_maxChunkDirty[c] = 0;
    }

    // 检查是否有在 Worker 运行期间新标记的脏 chunk（被保留未清除的）
    bool hasNewDirty = false;
    for (int c = 0; c < numChunks; ++c) {
        if (m_maxChunkDirty[c]) { hasNewDirty = true; break; }
    }

    if (hasNewDirty) {
        // 有新的脏 chunk，快照并触发新一轮重算（标志保持 true）
        m_maxWorker->setDirtyFlags(m_maxChunkDirty.data(), numChunks);
        QMetaObject::invokeMethod(m_maxWorker, "recomputeAsync", Qt::QueuedConnection);
    } else {
        // 所有脏 chunk 已处理完毕，释放待处理标志
        m_recomputePending = false;
    }

    // 通知频谱图：直接携带最新数据
    emit maxHoldAvgUpdated(m_maxWorker->getSharedMaxHold(),
                           m_maxWorker->getSharedAvgData(),
                           m_maxSpectrum);
}

// ============================================================================
// 最大谱 — 公开接口
// ============================================================================

const float *HQTfwaterfall::maxSpectrum() const
{
    // 确保后台重算完成后再返回
    const_cast<HQTfwaterfall *>(this)->ensureMaxValid();
    return m_maxSpectrum.data();
}

void HQTfwaterfall::ensureMaxValid()
{
    // 检查是否有脏 chunk 待处理
    bool hasDirty = false;
    for (size_t i = 0; i < m_maxChunkDirty.size(); ++i) {
        if (m_maxChunkDirty[i]) { hasDirty = true; break; }
    }

    if (!hasDirty) return;

    // 如果 Worker 线程正在运行，等待其完成
    // 通过在 UI 线程中直接执行重算来避免竞态条件
    // （Worker 可能正在读取 m_maxChunkDirty 和 m_circularBuf）

    // 简单策略：直接在当前线程中执行重算（与 Worker 做同样的事）
    // 由于我们已经在等待，直接计算比等待 Worker 更可控
    const int nx = m_fftlen;
    const int ny = m_showSize;
    const int chunkSize = MAX_SPECTRUM_CHUNK_SIZE;
    const int numChunks = static_cast<int>(m_maxChunkDirty.size());
    const float *circBuf = m_circularBuf.data();
    float *maxPtr = m_maxSpectrum.data();

    for (int c = 0; c < numChunks; ++c) {
        if (!m_maxChunkDirty[c]) continue;

        const int binStart = c * chunkSize;
        const int binEnd   = std::min(binStart + chunkSize, nx);
        const int binCount = binEnd - binStart;

        // 重置为 -inf
        const float negInf = -std::numeric_limits<float>::infinity();
        std::fill(maxPtr + binStart, maxPtr + binEnd, negInf);

        // 遍历所有活跃帧取最大值（加锁保护，避免与 Worker 线程或 post() 竞态）
        {
            QMutexLocker circLock(&m_circBufMutex);
            for (int r = 0; r < ny; ++r) {
                const float *frame = circBuf + r * nx + binStart;
                for (int b = 0; b < binCount; ++b) {
                    if (frame[b] > maxPtr[binStart + b]) {
                        maxPtr[binStart + b] = frame[b];
                    }
                }
            }
        }

        m_maxChunkDirty[c] = 0;
    }
}

void HQTfwaterfall::resetMaxSpectrum()
{
    // 将所有频点的最大值重置为 -inf
    const float negInf = -std::numeric_limits<float>::infinity();
    std::fill(m_maxSpectrum.begin(), m_maxSpectrum.end(), negInf);

    // 清除所有脏标记
    std::fill(m_maxChunkDirty.begin(), m_maxChunkDirty.end(), 0);

    // 重置帧计数
    m_framePostedCount = 0;

    // 重置 Worker 状态
    if (m_maxWorker) {
        m_maxWorker->reset();
    }
}

void HQTfwaterfall::addObj(HQSigMF::DetectionObject* obj)
{
    HQTFMarkStyle style = HQTFMarkStyle::Normal;
    if (obj->alarm_level == 1) {
        style = HQTFMarkStyle::Warning;
    } else if (obj->alarm_level == 2) {
        style = HQTFMarkStyle::Error;
    }

    // 按 ID 在使用中标记 map 中快速查找（O(log n)），存在则直接更新
    auto it = m_usedMarks.find(obj->id);
    if (it != m_usedMarks.end())
    {
        HQTFMark *mark = it->second;
        const int64_t nextStopMs = obj->t_end_ns / NS_PER_MS;
        int64_t nextStartMs = obj->t_start_ns / NS_PER_MS;
        if (mark->hasTimeRange() && nextStopMs > mark->timeStopMs())
        {
            freezeMark(mark);
            nextStartMs = qMax(nextStartMs, mark->timeStopMs());
        }
        mark->setFreqStart(obj->f_start_hz);
        mark->setFreqStop(obj->f_end_hz);
        mark->setStyle(style);
        mark->setCfText(QString::number((obj->f_start_hz + obj->f_end_hz) / 2000000, 'f', 1) + " MHz");
        mark->setBwText(QString::number((obj->f_end_hz - obj->f_start_hz) / 1000, 'f', 0) + " kHz");
        mark->setTimeRangeMs(nextStartMs, nextStopMs);
        return;
    }

    // 未找到：从空闲池获取一个标记（池不足时自动扩容），返回的指针直接改值
    HQTFMark *mark = acquireMark(obj->id);
    mark->setId(obj->id);
    mark->setFreqStart(obj->f_start_hz);
    mark->setFreqStop(obj->f_end_hz);
    mark->setStyle(style);
    mark->setCfText(QString::number((obj->f_start_hz + obj->f_end_hz) / 2000000, 'f', 1) + " MHz");
    mark->setBwText(QString::number((obj->f_end_hz - obj->f_start_hz) / 1000, 'f', 0) + " kHz");
    mark->setTimeRangeMs(obj->t_start_ns / 1e6, obj->t_end_ns / 1e6);
}

void HQTfwaterfall::finishObj(int64_t id)
{
    const auto it = m_usedMarks.find(id);
    if (it == m_usedMarks.end()) return;

    freezeMark(it->second);
    releaseMark(id);
    update();
}

// ============================================================================
// 标记对象池 — 空闲/使用中双 map 指针管理
// ============================================================================

void HQTfwaterfall::growMarkPool()
{
    // 按当前容量的 1.5 倍扩容（m_marks.size() 为当前池大小）
    // 对象池节点永不删除，size 单调递增，因此下一个可用索引 = m_marks.size()
    const int oldSize = static_cast<int>(m_marks.size());
    const int target  = qMax(oldSize * 3 / 2, HQ_TFWATERFALL_MARK_CAPACITY);

    // 追加新索引节点（既有节点地址不变，已登记的指针保持有效）
    for (int i = oldSize; i < target; ++i)
    {
        auto ret = m_marks.emplace(i, HQTFMark());
        m_freeMarks[i] = &ret.first->second;
        m_markPoolKeys[&ret.first->second] = i;
    }
}

void HQTfwaterfall::freezeMark(const HQTFMark *mark)
{
    if (!mark || !mark->hasTimeRange()) return;

    HQTFMark historical = *mark;
    historical.setSelected(false);
    m_historicalMarks.push_back(historical);

    // 有界保存；超过可视缓冲量级的最旧历史段不再参与绘制。
    const size_t maxHistory = static_cast<size_t>(qMax(4096, m_showSize * 32));
    while (m_historicalMarks.size() > maxHistory)
        m_historicalMarks.pop_front();
}

void HQTfwaterfall::returnMarkToPool(HQTFMark *mark)
{
    if (!mark) return;

    const auto keyIt = m_markPoolKeys.find(mark);
    if (keyIt == m_markPoolKeys.end()) return;

    mark->setSelected(false);
    mark->setId(0);
    mark->clearTimeRange();
    m_freeMarks[keyIt->second] = mark;
}

HQTFMark *HQTfwaterfall::acquireMark(int64_t id)
{
    // 空闲池为空说明池内标记已全部被使用，先按 1.5 倍扩容
    if (m_freeMarks.empty())
    {
        growMarkPool();
    }

    // 从空闲池取出一个空闲标记（对象池节点不动，指针永久有效），
    // 以 addObj 传入的真实 id 为 key 登记进使用中 map；
    // 返回值由调用方直接改值（setId、频率等）
    auto it = m_freeMarks.begin();
    HQTFMark *mark = it->second;
    m_freeMarks.erase(it);
    m_usedMarks[id] = mark;
    ++m_markCount;
    return mark;
}

void HQTfwaterfall::releaseMark(int64_t id)
{
    auto it = m_usedMarks.find(id);
    if (it == m_usedMarks.end()) return;

    HQTFMark *mark = it->second;
    m_usedMarks.erase(it);
    returnMarkToPool(mark);
    m_markCount = static_cast<int>(m_usedMarks.size());
}

void HQTfwaterfall::clearMarks()
{
    for (const auto &pkv : m_usedMarks)
    {
        returnMarkToPool(pkv.second);
    }
    m_usedMarks.clear();
    m_historicalMarks.clear();
    m_markCount = 0;
    update();
}
