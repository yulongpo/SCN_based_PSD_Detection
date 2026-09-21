#pragma once

#include <QCloseEvent>
#include <QDialog>
#include <QLabel>
#include <QMutex>
#include <QPushButton>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <QString>

#include <atomic>

#include "radioai/icd/SignalData.hpp"
#include "LoadingWidget.h"
#include "radioai/icd/HQSigMF.hpp"

class HQTfwaterfall;
class HQSpectrum;
class HQListBox;
class RecordProgressBar;
class QFrame;

class RecordPlaybackReaderAll : public QThread
{
public:
    /**
     * @param isWriteFinish 是否写入完毕
     * @param specSignals 需要计算标记的数据
     * @param specSignalsMutex 标记数据锁
     */
    RecordPlaybackReaderAll(std::atomic<bool>* isWriteFinish, QList<SignalDetailPerFileQueryResp::SignalItem*> *specSignals, QMutex *specSignalsMutex,
                    QVector<HQSigMF::DetectionObject> *lastObjs, QVector<std::pair<HQSigMF::DetectionObject, int>> *objs);

    /**
     * @brief 读取帧范围
     * @param writeAddr  写入数据指针地址
     * @param specPath   频谱数据文件路径
     * @param timePath   时间戳文件路径
     * @param specSize   单帧频谱点数（len_per_spec）
     * @param startFrame 开始帧
     * @param readTotalFrames 总帧数 <
     */
    void setWriteRange(float* writeAddr, const QString& specPath, const QString& timePath, int specSize, int startFrame, int readTotalFrames);

    /** @brief 请求线程安全退出 */
    void requestStop();

protected:
    void run() override;

private:
    QString              m_specPath;                                    ///< 频谱数据文件路径
    QString              m_timePath;                                    ///< 时间戳文件路径
    int                  m_specSize;                                    ///< 单帧频谱点数（len_per_spec）
    float               *m_writeAddr;                                   ///< 写入数据指针地址
    int                  m_readTotalFrames;                             ///< 读取总帧数
    int                  m_startFrame = 0;                              ///< 起始帧索引
    std::atomic<bool>   *m_isWriteFinish;                               ///< 是否写入完毕
    std::atomic<bool>    m_stop{false};                           ///< 停止请求标志
    QList<SignalDetailPerFileQueryResp::SignalItem*> *m_specSignals;    ///< 频谱缓存
    QMutex *m_specSignalsMutex;                                         ///< 频谱缓存锁
    QVector<HQSigMF::DetectionObject> *m_lastObjs;                      ///< seek时的最终obj用于给频谱图 多线程由m_isWriteFinish保证
    QVector<std::pair<HQSigMF::DetectionObject, int>> *m_objs;          ///< seek时计算的全部obj用于给列表框 多线程由m_isWriteFinish保证
};

/**
 * @brief 回放详情弹窗
 *
 * 无边框圆角弹窗，带有标题栏（回放+关闭按钮）、两行信息展示、
 * 时频瀑布图、频谱图和信号列表。
 * 圆角直接用 QPainterPath 绘制，不使用位图 mask，避免模糊锯齿。
 */
class RecordDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @brief 构造回放详情弹窗
     * @param data[in] 展示数据
     * @param signalPath[in] 频谱数据存储路径，为空时回退到默认相对路径
     * @param parent[in] 父控件
     */
    explicit RecordDialog(const SpectrumFileInfo& data,
                          const QString& signalPath = QString(),
                          QWidget *parent = nullptr);
    ~RecordDialog() override;

public slots:
    /**
     * @brief 信号列表
     * @param resp 信号列表
     */
    void dialogSignalDetail(const SignalDetailPerFileQueryResp& resp);

    /**
     * @brief 通告信号变化
     * @param type 告警等级 0-普通 1-一般 2-严重
     * @param num 数量
     */
    void addSignalNum(int type, int num);

    /** @brief 是否暂停
     * @param paused true:暂停
     */
    void slotPause(bool paused);

protected:
    void paintEvent(QPaintEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    /** @brief 设置标题栏（回放标题 + 关闭按钮） */
    void setupTitleBar();
    /** @brief 设置信息区和图表区 */
    void setupContent(const SpectrumFileInfo &data);
    /** @brief 应用主题色 */
    void applyTheme();
    /** @brief DPR 缩放辅助 */
    int  scaledPx(int designPx, int min = 0) const;
    /** @brief 把毫秒转为指定格式*/
    QString epochToTimeStr(int64_t epochMs);

    // ========================================================================
    // 回放
    // ========================================================================
    /**
     * @brief 启动回放（打开文件、启动读取线程和消费定时器）
     * @param startFrame 起始帧索引，默认为 0（从头开始播放）
     */
    void startPlayback(int startFrame = 0);
    /** @brief 停止回放（停止线程、清理缓冲） */
    void stopPlayback();
    /**
     * @brief 拖拽进度条时 seek 到指定帧索引位置
     * @param targetFrame 目标帧索引（0 ~ spec_num-1）
     * @note 进度条已改用帧数映射，直接接收帧索引，无需二分查找
     */
    void seekToPosition(int targetFrame);
    /**
     * @brief 计算范围内数据的最大最小值
     * @param data 数据
     * @param startIndex 起始下标
     * @param endIndex 结束下标
     * @param min 最小值
     * @param max 最大值
     */
    inline void getMinMax(float *data, int startIndex, int endIndex, double &min, double &max);

private:
    // ========================================================================
    // 标题栏
    // ========================================================================
    QLabel      *m_titleLabel    = nullptr;  ///< 标题
    QLabel      *m_accentLine    = nullptr;  ///< 蓝色竖线
    QPushButton *m_closeBtn      = nullptr;  ///< 关闭按钮
    QVBoxLayout *m_mainLayout    = nullptr;  ///< 主布局
    QWidget     *m_titleBar      = nullptr;  ///< 标题栏容器

    // ========================================================================
    // 信息区（顶部圆角面板）
    // ========================================================================
    QWidget     *m_infoPanel     = nullptr;  ///< 信息区圆角面板
    QWidget     *m_contentWidget = nullptr;  ///< 信息内容区（面板内部）

    // 第一行值标签
    QLabel *m_filenameVal      = nullptr;
    QLabel *m_centerFreqVal    = nullptr;
    QLabel *m_bandwidthVal     = nullptr;
    QLabel *m_deviceNameVal    = nullptr;
    QLabel *m_rbwVal           = nullptr;

    // 第二行值标签
    QLabel *m_refLevelVal      = nullptr;
    QLabel *m_lenPerSpecVal     = nullptr;
    QLabel *m_startTimeVal     = nullptr;
    QLabel *m_endTimeVal       = nullptr;
    QLabel *m_recordStatusVal  = nullptr;

    // 统计标签（信息区右侧）
    QLabel *m_totalCountLabel  = nullptr;  ///< "信号总数：X"
    QLabel *m_generalAlertLabel = nullptr; ///< "一般警告：X"
    QLabel *m_severeAlertLabel = nullptr;  ///< "严重警告：X"

    // ========================================================================
    // 图表区（底部圆角面板，瀑布图 + 频谱图 + 信号列表框）
    // ========================================================================
    QWidget       *m_graphPanel   = nullptr;  ///< 图表区圆角面板
    HQTfwaterfall *m_waterfall  = nullptr;  ///< 时频瀑布图
    HQSpectrum    *m_spectrum   = nullptr;  ///< 频谱图
    RecordProgressBar *m_progressBar = nullptr;  ///< 进度条
    HQListBox     *m_listBox    = nullptr;  ///< 信号列表
    QFrame        *m_separator1 = nullptr;  ///< 瀑布图与频谱图分隔线
    QFrame        *m_separator2 = nullptr;  ///< 频谱图与进度条分隔线
    QFrame        *m_separator3 = nullptr;  ///< 进度条与列表分隔线

    // ========================================================================
    // 回放 — 三缓冲 + 线程
    // ========================================================================
    QThread *m_playbackThread      = nullptr;   ///< 文件读取线程
    QThread *m_playbackAllThread   = nullptr;   ///< 文件读取线程-加载多帧
    QTimer  *m_playbackConsumeTimer = nullptr;  ///< 主线程消费定时器（30 FPS）
    QTimer  *m_playbackConsumeAllTimer = nullptr;  ///< 主线程消费定时器（10）

    float              *m_slotData = nullptr;             ///< 缓冲频谱数据（每槽 len_per_spec 个 float）
    int64_t             m_slotTime = 0;                   ///< 缓冲时间戳
    std::atomic<int>    m_readySlot{-1};                             ///< 就绪标志：-1=无就绪, 0就绪
    bool                m_xRangeSyncing  = false;         ///< X 轴同步中，防递归
    bool                m_firstFrame = true;              ///< 用以第一次计算色阶

    QPoint  m_dragPosition;
    bool    m_dragging = false;

    SpectrumFileInfo m_fileInfo;
    QString          m_signalPath;  ///< 频谱数据存储路径（后端 spectrum_dir 配置）
    SignalDetailPerFileQueryResp m_signalDetails;
    QList<SignalDetailPerFileQueryResp::SignalItem*> m_specSignals;// 频谱缓存
    QMutex m_specSignalsMutex; // 频谱缓存锁
    QVector<HQSigMF::DetectionObject> m_lastObjs; // seek时的最终obj用于给频谱图 多线程由m_isWriteFinish保证
    QVector<std::pair<HQSigMF::DetectionObject, int>> m_objs;// seek时计算的全部obj用于给列表框 多线程由m_isWriteFinish保证
    QVector<int64_t>    m_allTimestamps;   ///< 预加载的全部时间戳（纳秒），用于 seek 二分查找
    int                 m_timeIndex;       ///< 时间下标
    int                 m_currentPlayFrame = 0; ///< 当前播放/预览的帧索引，恢复播放时从此帧+1开始
    bool                m_rangeConfigured = false; ///< 瀑布图/频谱图分辨率是否已配置（避免重配清空预览数据）
    std::atomic<bool>   m_isPaused{false};  ///< 暂停标志
    std::atomic<bool>   m_isSeeking{false}; ///< 正在 seek 中，消费定时器需跳过旧数据
    std::atomic<bool>   m_isWriteFinish{false};          ///< 是否写入完毕用于一次性加载

    // 加载动画
    LoadingWidget *m_loadingWidget = nullptr;
};
