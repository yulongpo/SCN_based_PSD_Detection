#pragma once

#include "../plot/SpecPlotBase.h"
#include "HQTFMark.h"

#include <vector>
#include <map>
#include <deque>
#include <cstring>
#include <cstdint>
#include <atomic>

#include <QThread>
#include <QMutex>
#include <QFrame>
#include <QPushButton>
#include <QPoint>

#include "radioai/icd/HQSigMF.hpp"

class QCPColorMap;
class QCPColorGradient;
class QResizeEvent;
class QMouseEvent;

/// 时频图标记框预分配容量
#define HQ_TFWATERFALL_MARK_CAPACITY 1024

/**
 * @brief 时频图 Y 轴时间标签生成器
 *
 * 仅重写 getTickLabel，将刻度值（秒）转换为时间字符串。
 * 刻度位置完全由父类 QCPAxisTicker 标准算法决定（与原版行为一致）。
 */
class WaterfallTimeTicker : public QCPAxisTicker
{
public:
    void updateState(const std::vector<int64_t> *timeBuf, int showSize,
                     int oldestRow, const QCPRange &timeRange)
    {
        m_timeBuf   = timeBuf;
        m_showSize  = showSize;
        m_oldestRow = oldestRow;
        m_timeRange = timeRange;
    }

protected:
    QString getTickLabel(double tick, const QLocale &locale, QChar formatChar, int precision) override;

private:
    const std::vector<int64_t> *m_timeBuf   = nullptr;
    int     m_showSize  = 0;
    int     m_oldestRow = 0;
    QCPRange m_timeRange;
};

class HQTfwaterfall;

// ============================================================================
// MaxSpectrumWorker — 后台线程执行最大谱重算
// ============================================================================

/**
 * @brief 最大谱计算工作线程
 *
 * 将 UI 线程标记的脏 chunk 在后台重新计算（从循环缓冲区遍历所有活跃帧取最大值），
 * 完成后通过信号通知 UI 线程合并结果。避免 UI 线程因大规模浮点比较而卡顿。
 */
class MaxSpectrumWorker : public QObject
{
    Q_OBJECT

public:
    explicit MaxSpectrumWorker(QObject *parent = nullptr);
    ~MaxSpectrumWorker() override = default;

    /** @brief 初始化/重新分配缓冲区 */
    void init(int fftlen, int showSize, const float *circBuf, QMutex *circBufMutex,
              const int64_t *frameCntPtr, std::atomic<int64_t> *workerProcessedFramePtr);

    /** @brief 重置所有状态（对应 clearData） */
    void reset();

    /**
     * @brief UI 线程在触发重算前调用，将当前脏标记快照到 Worker 内部
     * @param flags  脏标记数组（长度 = numChunks）
     * @param count  数组长度
     * @note  线程安全（内部互斥锁保护）
     */
    void setDirtyFlags(const uint8_t *flags, int count);

    /** @brief 获取最近一次重算完成的最大谱（线程安全，通过互斥锁保护） */
    std::vector<float> getSharedMax() const;

    /** @brief 获取最近一次实际处理过的 chunk 标记（线程安全） */
    std::vector<uint8_t> getProcessedChunks() const;

    /** @brief 获取最大保持数据（线程安全） */
    std::vector<float> getSharedMaxHold() const;

    /** @brief 获取运行平均数据（线程安全） */
    std::vector<float> getSharedAvgData() const;

public slots:
    /** @brief 由 UI 线程通过 QueuedConnection 触发，在后台线程中执行重算 */
    void recomputeAsync();

signals:
    /** @brief 重算完成，UI 线程应调用 mergeRecomputeResult() 合并结果 */
    void recomputeFinished();

private:
    // ---- 配置（init 时设置，之后只读） ----
    const float   *m_circBuf     = nullptr;  ///< 指向 HQTfwaterfall::m_circularBuf（只读访问）
    QMutex        *m_circBufMutex = nullptr; ///< 保护 m_circularBuf 的互斥锁（跨线程共享）
    int m_fftlen   = 0;
    int m_showSize = 0;
    int m_numChunks = 0;

    // ---- 脏标记快照（UI 线程通过 setDirtyFlags() 写入，Worker 线程在 recomputeAsync() 中读取） ----
    mutable QMutex     m_snapshotMutex;
    std::vector<uint8_t> m_dirtySnapshot; ///< Worker 自有的脏标记副本（不跨线程共享原始指针）

    // ---- 帧处理追踪 ----
    const int64_t *m_framePostedCountPtr = nullptr; ///< 指向 HQTfwaterfall::m_framePostedCount
    std::atomic<int64_t> *m_workerProcessedFramePtr = nullptr; ///< 指向 HQTfwaterfall::m_workerProcessedFrame
    int64_t        m_lastProcessedFrame  = -1;      ///< 上次处理到的全局帧索引

    // ---- 最大保持（所有历史帧，仅后台线程访问） ----
    std::vector<float> m_maxHold;

    // ---- 运行平均（所有历史帧，仅后台线程访问） ----
    std::vector<float> m_avgSum;
    std::vector<float> m_avgData;
    int64_t            m_avgCount = 0;

    // ---- 工作缓冲区（仅后台线程访问） ----
    std::vector<float> m_localMax;        ///< 重算过程中使用的局部最大谱

    // ---- 共享结果（互斥锁保护，UI 线程通过 getSharedMax() 读取） ----
    mutable QMutex    m_resultMutex;
    std::vector<float> m_sharedMax;         ///< 滑动窗口最大谱结果
    std::vector<float> m_sharedMaxHold;     ///< 最大保持结果
    std::vector<float> m_sharedAvgData;     ///< 运行平均结果
    std::vector<uint8_t> m_processedChunks; ///< 最近一次实际处理过的 chunk 标记

    static constexpr int CHUNK_SIZE = 4096;
};

// ============================================================================
// HQTfwaterfall — 时频瀑布图控件
// ============================================================================

/**
 * @brief 时频瀑布图控件
 */
class HQTfwaterfall : public SpecPlotBase
{
    Q_OBJECT

public:
    explicit HQTfwaterfall(QWidget *parent = nullptr);
    ~HQTfwaterfall() override;

    void setResolution(int fftlen, int showSize,
                       const QCPRange &freqRange, const QCPRange &timeRange);
    void post(const int64_t time, const float *spectrum);
    void clearData();
    void setDecimationEnabled(bool enabled);
    void setColorDataRange(double min, double max);
    QCPColorMap *colorMap() const { return m_colorMap; }
    void setInterpolate(bool enabled);
    void setTightBoundary(bool enabled);
    static QString formatTimestamp(int64_t timeMs);

    // ========================================================================
    // 最大谱接口
    // ========================================================================

    /**
     * @brief 获取当前最大谱数据指针（只读）
     * @return 指向 m_maxSpectrum 的 const float* 指针，长度等于 m_fftlen
     * @note  内部自动调用 ensureMaxValid() 等待后台重算完成
     */
    const float *maxSpectrum() const;

    /**
     * @brief 强制立即完成所有脏 chunk 的重算，使最大谱数据完全一致
     * @note  会阻塞调用线程直到后台 Worker 完成（通常由 maxSpectrum() 内部调用）
     */
    void ensureMaxValid();

    /**
     * @brief 重置最大谱（清空所有累积的最大值）
     */
    void resetMaxSpectrum();

    // ========================================================================
    // 标记管理接口
    // ========================================================================

    /** @brief 添加一个标记框，返回标记的索引 */
    void addObj(HQSigMF::DetectionObject* obj);
    /**
     * @brief 结束指定业务 ID 的活动段
     *
     * 将最后一段冻结为历史记录并归还对象池。重复调用安全。
     */
    void finishObj(int64_t id);
    /** @brief 清空所有标记框（全部标记归还空闲池并刷新） */
    void clearMarks();

    /**
     * @brief 从空闲池获取一个标记指针（池不足时自动按 1.5 倍扩容）
     * @param id[in] addObj 传入的真实业务 id（obj->id），作为使用中 map 的 key
     * @return 指向 HQTFMark 的指针（对象池节点固定，指针永久有效），
     *         由调用方直接改值（setId、频率、时间范围等）
     */
    HQTFMark *acquireMark(int64_t id);

    /**
     * @brief 释放一个标记 id 回空闲池
     * @param id[in] 由 acquireMark() 获取的标记 id
     */
    void releaseMark(int64_t id);

    /**
     * @brief 将 m_timeBuf 时间戳转换为 Y 轴像素坐标
     * @param targetMs 目标时间戳（ms，对应 m_timeBuf 中存储的值）
     * @return 对应的 Y 轴像素坐标；若时间戳已滚出缓冲区则返回 -1.0
     */
    double timeMsToPixelY(int64_t targetMs) const;

    /**
     * @brief 获取当前最旧帧的环形缓冲区索引
     * @note  供外部模块了解缓冲区状态使用
     */
    int oldestRow() const { return m_writeRow % m_showSize; }

    /**
     * @brief 获取 m_timeBuf 的引用
     * @note  供外部模块查询/修改时间戳使用
     */
    std::vector<int64_t> &timeBuf() { return m_timeBuf; }

    /**
     * @brief 获取 Y 轴数据范围（m_timeRange）
     */
    QCPRange timeRange() const { return m_timeRange; }

    /**
     * @brief 获取时频图数据
     * @return float* 返回时频图数据
     */
    float* tfData() { return m_circularBuf.data(); }

    /**
     * @brief 刷新UI并且重新计算最大谱
     * @param writeRow 设置写入数据的行数
     */
    void updateTimesAndMax(int writeRow);

    /**
     * @brief 设置界面是否可操作
     * @param enable true 允许操作；false 禁止操作（冻结视图，防误触）
     *
     * 禁用时通过 QWidget::setEnabled(false) 阻止所有鼠标/滚轮事件，
     * 从而禁用时频图的拖拽、缩放等交互，子控件（若有）也一并禁用。
     */
    void setOperatorEnabled(bool enable);

public slots:
    /**
     * @brief 响应列表行单击：在时频图上定位/高亮对应的标记框
     * @param id         检测对象 ID
     * @param freqStart  起始频率（Hz）
     * @param freqStop   截止频率（Hz）
     * @param alarmLevel 告警等级（0=正常, 1=一般, 2=严重）
     *
     * 如果 ID 在当前 m_usedMarks 中存在，则高亮该标记并取消其他高亮；
     * 如果不存在（该信号尚未出现在时频图当前帧），则取消所有高亮。
     */
    void onRowSelectedForMark(int64_t id, qint64 freqStart, qint64 freqStop, int alarmLevel);

    /**
     * @brief 响应另一张图的选中同步（markSelectionChanged）
     * @param id 外部选中的检测对象 ID；-1 表示全部取消选中
     *
     * 只更新本图标记的选中状态，不反向发信号（避免循环）。
     */
    void onExternalMarkSelected(int64_t id);

    /**
     * @brief 设置标记框是否可见（可由外部程序控制）
     * @param visible true 显示标记框；false 隐藏标记框
     */
    void setMarksVisible(bool visible);

    /** @brief 获取当前标记框显示状态。 */
    bool marksVisible() const { return m_marksVisible; }

signals:
    /**
     * @brief Worker 完成重算后发出，携带最新最大保持和平均数据
     * @param maxHold 最大保持数据
     * @param avgData 运行平均数据
     * @param TfMaxHold 时频图最大保持数据
     */
    void maxHoldAvgUpdated(const std::vector<float> &maxHold, const std::vector<float> &avgData, const std::vector<float> &TfMaxHold);

    /**
     * @brief 发送最后一帧的时间戳（用于列表行过期清理）
     * @param timeMs 最后一帧的时间戳（epoch 毫秒）
     */
    void lastFrameTimeUpdated(int64_t timeMs);

    /**
     * @brief 时频图标记框被点击时发出，供列表反向定位同步
     * @param id 被点击的检测对象 ID
     */
    void markClicked(int64_t id);

    /**
     * @brief 标记选中状态变化时发出，供另一张图同步选中
     * @param id 当前选中的检测对象 ID；-1 表示点击空白处，全部取消选中
     */
    void markSelectionChanged(int64_t id);


protected:
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;   // 手绘左侧帧序号 + 标记框

    /**
     * @brief 重写鼠标按下事件，处理标记框选中
     * @param event 鼠标事件对象
     */
    void mousePressEvent(QMouseEvent *event) override;

    /**
     * @brief 根据轴区域更新悬停光标，标记框上显示手型
     */
    void updateHoverCursor(AxisZone zone, const QPoint &mousePos) override;

private slots:
    /**
     * @brief Worker 线程完成重算后的回调（QueuedConnection，在 UI 线程执行）
     *
     * 从 Worker 读取重算结果并合并到 m_maxSpectrum 中，清除脏标记。
     */
    void onRecomputeFinished();

    /**
     * @brief 右上角"标记"按钮选中状态切换：控制时频图标记框显隐
     * @param checked true 显示标记框；false 隐藏标记框
     */
    void onBtnLabelToggled(bool checked);

private:
    void initGradient();
    void setupColorMap();
    void applyThemeColors();

    /**
     * @brief 创建右上角"标记"悬浮按钮栏（仅一个按钮，控制标记框显隐）
     */
    void setupToolButtons();

    /**
     * @brief 更新"标记"按钮的选中/未选中样式
     */
    void updateToolButtonStyle();

    /**
     * @brief 根据鼠标像素位置更新右上角按钮栏的显隐
     *
     * 显示区域 = 按钮栏控件范围，高度方向扩展为按钮高度的三倍；
     * 鼠标位于该区域内时显示按钮栏，否则隐藏。
     */
    void updateToolBarVisibility();

    // 最大谱 — UI 线程部分（轻量操作）
    void updateMaxSpectrum(const float *spectrum);
    void handleMaxDiscard(const float *oldFrame);

    /**
     * @brief 刷新时频图数据
     */
    void renderWaterfall();

    /**
     * @brief 在 paintEvent 中绘制所有标记框
     * @param painter   已初始化的 QPainter（在 QCustomPlot::paintEvent 之后）
     * @param oldestRow 当前最旧帧的环形缓冲区索引
     */
    void drawMarks(QPainter &painter, int oldestRow);

    /**
     * @brief 点击检测并更新标记框选中状态
     * @param pos 鼠标点击的像素坐标
     */
    void selectMarkAt(const QPoint &pos);

    /**
     * @brief 扩容标记对象池（按当前容量的 1.5 倍扩容，新对象指针全部加入空闲 map）
     */
    void growMarkPool();

    /** @brief 将活动标记当前段复制到历史段队列。 */
    void freezeMark(const HQTFMark *mark);

    /** @brief 按固定池索引将对象归还空闲池并清理易残留状态。 */
    void returnMarkToPool(HQTFMark *mark);

    /** @brief 获取当前瀑布缓冲区中最早和最新的有效时间戳。 */
    bool visibleTimeBounds(int64_t& oldestMs, int64_t& newestMs) const;

    /** @brief 将标记时间段裁剪到可见时间窗并换算为像素范围。 */
    bool timeRangeToPixelY(int64_t startMs, int64_t stopMs,
                           double& yTop, double& yBottom) const;

    // ---- 控件成员 ----
    QCPColorMap      *m_colorMap    = nullptr;
    QCPColorGradient  m_gradient;

    int m_fftlen      = 4096;
    int m_showSize    = 512;
    int m_writeRow    = 0;
    int m_dispCols    = 0;
    QCPRange m_freqRange;
    QCPRange m_timeRange;
    bool     m_enableDecimation = true;
    double   m_lastVisLower    = 0.0;
    double   m_lastVisUpper    = 0.0;
    std::vector<float> m_circularBuf;
    std::vector<int64_t> m_timeBuf;

    // ---- 最大谱 — UI 线程数据 ----
    static constexpr int MAX_SPECTRUM_CHUNK_SIZE = 4096;   ///< 最大谱分块大小（频点数/chunk）
    std::vector<float>   m_maxSpectrum;                     ///< 当前最大谱（UI 线程维护，逐频点最大值）
    std::vector<uint8_t> m_maxChunkDirty;                   ///< 每个 chunk 的脏标记（UI 线程标记，Worker 读取后由 UI 清除）
    int64_t              m_framePostedCount = 0;            ///< 已推送总帧数（用于判断是否发生覆盖）

    // ---- Worker 进度追踪（原子变量，跨线程无锁读写） ----
    std::atomic<int64_t> m_workerProcessedFrame{-1};  ///< Worker 已处理到的全局帧索引

    // ---- 后台线程 ----
    QThread            *m_maxThread        = nullptr;       ///< 最大谱重算后台线程
    MaxSpectrumWorker  *m_maxWorker        = nullptr;       ///< 工作对象（living in m_maxThread）
    QMutex              m_circBufMutex;                     ///< 保护 m_circularBuf 跨线程读写
    std::atomic<bool>   m_recomputePending{false};          ///< 是否有重算信号在 Worker 队列中待处理

    QSharedPointer<WaterfallTimeTicker> m_timeTicker;

    std::map<int, HQTFMark>       m_marks;      ///< 标记对象池（key = 固定池内索引，节点永不迁移）
    std::map<int, HQTFMark*>      m_freeMarks;  ///< 空闲标记指针 map（key = 池内索引）
    std::map<HQTFMark*, int>      m_markPoolKeys; ///< 标记指针到固定池内索引的反向映射
    std::map<int64_t, HQTFMark*>  m_usedMarks;  ///< 使用中标记指针 map（key = addObj 真实 id）
    std::deque<HQTFMark>          m_historicalMarks; ///< 已冻结的历史频率段，后续带宽变化不回写
    bool                          m_marksVisible = false; ///< 标记框是否可见（由时频图右上角"标记"按钮控制）
    int                           m_markCount = 0;  ///< 使用中的标记数

    // ---- 右上角"标记"悬浮按钮栏 ----
    QFrame      *m_buttonBar     = nullptr;   ///< 按钮容器
    QPushButton *m_btnLabel      = nullptr;   ///< "标记"按钮
    int          m_toolBarHeight = 0;        ///< 按钮栏高度（setupToolButtons 时记录，用于像素判断）
    QPoint       m_lastMousePos;             ///< 最近一次鼠标位置（像素坐标），用于按钮栏显隐判断
};
