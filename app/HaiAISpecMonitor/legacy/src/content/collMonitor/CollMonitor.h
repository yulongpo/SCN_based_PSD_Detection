#pragma once

#include <QWidget>
#include <QPixmap>
#include <QVBoxLayout>
#include <QTimer>
#include <QVector>
#include <QQueue>
#include <QMutex>
#include <QElapsedTimer>
#include <memory>
#include "radioai/icd/HQSigMF.hpp"
#include "radioai/icd/SignalData.hpp"
#include "DetectionOverlayModel.h"

class HQTfwaterfall;
class HQSpectrum;
class HQListBox;
class QFrame;
class HQSplitter;

/**
 * @brief 通用圆角面板控件（采集监测面板）
 *
 * 绘制一个带圆角边框的 widget，圆角半径 24px，边框颜色 rgba(30,30,40,1)，
 * 内部承载时频瀑布图等监测子控件。
 */
class CollMonitor : public QWidget
{
    Q_OBJECT

public:
    explicit CollMonitor(QWidget *parent = nullptr);

    /**
     * @brief 向处理队列中添加 HQSigMF 数据（线程安全）
     * @param data HQSigMF 数据共享指针
     */
    void enqueueIcdData(QSharedPointer<HQSigMF> data);

signals:
    /**
     * @brief 通告信号变化
     * @param type 告警等级 0-普通 1-一般 2-严重
     * @param num 数量
     */
    void addSignalNum(int type, int num);

    /**
     * @brief 表格行双击信号（上层打开 RecordDialog 等操作）
     */
    void signalRowDoubleClicked();

    /**
     * @brief 将检测累积进度发送到底部状态栏显示。
     * @param stage DetectionStage 的整数值
     * @param accumulatedFrames 当前已累积帧数
     * @param minOutputFrames 快速检测最小帧数
     * @param requiredFrames 完整长窗帧数
     */
    void detectionProgressChanged(int stage, int accumulatedFrames,
                                  int minOutputFrames, int requiredFrames);

    /** @brief 通知底部状态栏清除实时检测状态。 */
    void detectionProgressCleared();

public slots:
    /**
     * @brief 得到基本参数
     * @param fc 中心频率
     * @param bw 带宽
     * @param refLevel 参考电平
     */
    void slotParams(int64_t fc, int64_t bw, int64_t refLevel);

    /**
     * @brief 是否刷新实时数据
     * @param flag true:刷新数据
     */
    void slotIsUpdateData(bool flag);

    /**
     * @brief 清空当前数据状态
     */
    void slotClearData();

    /**
     * @brief 设置列表框双击回放
     * @param flag true 允许双击回放、
     */
    void slotListDbRecord(bool flag);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void initUI();

    void initConnections();

    /**
     * @brief 初始化数据处理定时器
     *
     * 启动定时器，以固定帧率从 HQSigMF 队列中取出数据并推送至瀑布图和频谱图。
     */
    void initDataProcessing();

    /** 从当前数据包读取并转发检测进度；没有对应 FEATURE 时保持现有状态。 */
    void updateDetectionProgress(const QSharedPointer<HQSigMF> &signal);

    /** 通知底部状态栏清除检测进度。 */
    void resetDetectionProgress();

    /** 将显示模型的当前插值结果刷新到频谱标记层。 */
    void renderDetectionOverlay();

    /**
     * @brief 计算范围内数据的最大最小值
     * @param data 数据
     * @param startIndex 起始下标
     * @param endIndex 结束下标
     * @param min 最小值
     * @param max 最大值
     */
    inline void getMinMax(float *data, int startIndex, int endIndex, double &min, double &max);

private slots:
    /**
     * @brief 定时器回调：从队列取出 HQSigMF 并推送频谱数据
     *
     * 从 HQSigMF 中提取 FREQUENCY 域的短时平均谱数据，
     * 根据 config() 中的中心频率和采样率计算频率范围，
     * 将频谱数据推送至瀑布图和频谱图显示。
     */
    void onProcessTick();

private:
    HQTfwaterfall *m_waterfall      = nullptr;       ///< 时频瀑布图控件
    HQSpectrum    *m_spectrum       = nullptr;       ///< 频谱图控件
    HQSplitter    *m_splitter       = nullptr;       ///< 频谱图与时频图之间的垂直分裂器
    bool           m_xRangeSyncing  = false;         ///< X 轴同步中，防递归
    QFrame        *m_separatorList  = nullptr;       ///< 频谱图与列表框之间的分隔线
    HQListBox     *m_listBox        = nullptr;       ///< 信号列表框

    // ========================================================================
    // HQSigMF 数据队列及处理状态
    // ========================================================================
    QTimer                          *m_processTimer      = nullptr;   ///< 数据处理定时器
    int                              m_freqBins          = 4096;      ///< 频率方向格点数fftlen
    int64_t                          m_freqStart         = 0;         ///< 当前频率起始值（Hz）
    int64_t                          m_freqEnd           = 1000;      ///< 当前频率结束值（Hz）
    float                            m_refLevel          = 0;         ///< 参考电平（dBm）
    int                              m_timeBins          = 200;       ///< 时间方向格点数（可视行数）

    QQueue<QSharedPointer<HQSigMF>>  m_sigQueue;                      ///< HQSigMF 输入队列
    QMutex                           m_queueMutex;                    ///< 队列互斥锁（跨线程入队）
    QSharedPointer<HQSigMF>          m_currentSig;                    ///< 当前正在处理的 HQSigMF
    int                              m_currentFrameIdx  = 0;          ///< 当前帧索引
    int                              m_framesInCurrent  = 0;          ///< 当前 HQSigMF 中的总帧数
    bool                             m_firstFrame       = true;       ///< 是否是第一帧，用于计算色阶
    std::atomic<bool>                m_updateData;                    ///< 是否刷新最新数据
    DetectionOverlayModel            m_detectionOverlay;             ///< 仅负责 UI 边界插值与淡入淡出
    QElapsedTimer                    m_overlayClock;                  ///< UI 插值的单调时钟
};
