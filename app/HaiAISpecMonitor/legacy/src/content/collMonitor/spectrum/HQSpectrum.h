#pragma once

#include "../plot/SpecPlotBase.h"
#include "HQMark.h"
#include <QPushButton>
#include <QFrame>
#include <QVector>
#include <QLinearGradient>
#include <QPaintEvent>
#include <QElapsedTimer>
#include <QCursor>

#include "radioai/icd/HQSigMF.hpp"

class QVariantAnimation;
class QPainter;

/// 频谱图标记框预分配容量：初始化时构建此数量的 HQMark 对象，避免 addObj 时频繁申请内存
#define HQ_SPECTRUM_MARK_CAPACITY 1024

class QCPGraph;
class HQTfwaterfall;

/**
 * @brief 频谱图控件（1D 功率-频率折线图）
 *
 * 继承自 SpecPlotBase，使用 QCPGraph 绘制实时频谱。
 * - X 轴：频率（水平方向）
 * - Y 轴：功率/幅度（dB）
 * - 单条谱线，跟随 post() 推送的实时数据更新
 *
 * 外观与 HQTfwaterfall 时频图保持一致的暗色风格。
 */
class HQSpectrum : public SpecPlotBase
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param parent 父控件
     *
     * 自动初始化坐标轴、谱线和颜色主题。
     */
    explicit HQSpectrum(QWidget *parent = nullptr);

    /** @brief 析构函数 */
    ~HQSpectrum() override = default;

    // ========================================================================
    // 数据接口
    // ========================================================================

    /**
     * @brief 推送一帧频谱数据，更新谱线显示
     * @param spectrum 频谱数据数组（float），长度应等于 m_fftlen
     *
     * 内部构建 QCPGraph 的 key/value 数据对并立即 replot。
     */
    void post(const float *spectrum);

    /**
     * @brief 推送一帧频谱数据（QVector 重载）
     * @param spectrum 频谱数据向量
     */
    void post(const QVector<float> &spectrum);

    /**
     * @brief 设置频谱数据点数（X 轴分辨率）
     * @param fftlen   频率格点数
     * @param freqRange 频率轴范围
     */
    void setResolution(int fftlen, const QCPRange &freqRange);

    /**
     * @brief 清空谱线数据
     */
    void clearData();

    /**
     * @brief 添加检测标记
     * @param obj   检测对象
     */
    void addObj(HQSigMF::DetectionObject* obj, double opacity = 1.0);

    /**
     * @brief 清空标记
     */
    void clearMarks();

    /**
     * @brief 隐藏最大谱和平均谱
     */
    void hideMaxAndAvgBtn();

    /**
     * @brief 设置界面是否可操作
     * @param enable true 允许操作
     */
    void setOperatorEnabled(bool enable);

    /**
     * @brief 设置频谱游标是否显示
     * @param visible true 显示鼠标跟随游标
     */
    void setTracerVisible(bool visible);

public slots:
    /** @brief 接收 Worker 传来的最大保持+平均数据，直接刷新显示 */
    void onMaxHoldAvgUpdated(const std::vector<float> &maxHold,
                             const std::vector<float> &avgData,
                             const std::vector<float> &TfMaxHold);

    /**
     * @brief 响应列表行单击：在频谱图上定位/高亮对应的标记框
     * @param id         检测对象 ID
     * @param freqStart  起始频率（Hz）
     * @param freqStop   截止频率（Hz）
     * @param alarmLevel 告警等级（0=正常, 1=一般, 2=严重）
     *
     * 如果 ID 在当前 m_marks 中存在，则高亮该标记并取消其他高亮；
     * 如果不存在，则将数据赋值给 m_offlineMark 并以 OffLine 灰色样式高亮显示。
     */
    void onRowSelectedForMark(int64_t id, qint64 freqStart, qint64 freqStop, int alarmLevel);

    /**
     * @brief 响应另一张图的选中同步（markSelectionChanged）
     * @param id 外部选中的检测对象 ID；-1 表示全部取消选中
     *
     * 只更新本图标记的选中状态，不反向发信号（避免循环）。
     */
    void onExternalMarkSelected(int64_t id);

signals:
    /**
     * @brief 频谱图标记框被点击时发出，供列表反向定位同步
     * @param id 被点击的检测对象 ID
     */
    void markClicked(int64_t id);

    /**
     * @brief 标记选中状态变化时发出，供另一张图同步选中
     * @param id 当前选中的检测对象 ID；-1 表示点击空白处，全部取消选中
     */
    void markSelectionChanged(int64_t id);

protected:
    /**
     * @brief 重写绘制事件，在 QCustomPlot 渲染完成后绘制标记框
     * @param event 绘制事件对象
     */
    void paintEvent(QPaintEvent *event) override;

    /**
     * @brief 重写鼠标按下事件，处理标记框选中
     * @param event 鼠标事件对象
     */
    void mousePressEvent(QMouseEvent *event) override;

    /**
     * @brief 根据轴区域更新悬停光标，标记框上显示手型
     */
    void updateHoverCursor(AxisZone zone, const QPoint &mousePos) override;

    /**
     * @brief 重写鼠标移动事件，支持拖动选择频率范围
     * @param event 鼠标事件对象
     */
    void mouseMoveEvent(QMouseEvent *event) override;

    /**
     * @brief 重写鼠标释放事件，根据拖动方向执行频率缩放
     * @param event 鼠标事件对象
     */
    void mouseReleaseEvent(QMouseEvent *event) override;

    /**
     * @brief 鼠标离开控件时隐藏游标
     */
    void leaveEvent(QEvent *event) override;

private:
    /**
     * @brief 从主题配置读取并应用轴颜色（刻度标签 + 轴标签）
     */
    void applyThemeColors();

    /**
     * @brief 从 m_lastSpectrum 完整数据渲染到 QCPGraph
     */
    void renderSpectrum();

    /**
     * @brief 从 时频图来的最大谱扥 完整数据渲染到 QCPGraph
     */
    void maxRenderSpectrum();

    /**
     * @brief 根据当前 X 轴范围更新可见 bin 区间缓存
     */
    void updateVisBinRange();

    /**
     * @brief 点击检测并更新标记框选中状态
     * @param pos 鼠标点击的像素坐标
     */
    void selectMarkAt(const QPoint &pos);

    /**
     * @brief 通用缩放动画：从当前 X 轴范围平滑过渡到以 centerFreq 为中心、bandwidth × 10 为宽度的目标范围
     * @param centerFreq 目标中心频率（Hz）
     * @param bandwidth  信号带宽（Hz），视图宽度 = bandwidth × 10
     *
     * 使用 QVariantAnimation 实现 500ms 的 OutCubic 缓出动画，
     * 在列表行单击定位和频谱图标记框点击时均可调用。
     */
    void animateZoomToFreqRange(double centerFreq, double bandwidth);

    /**
     * @brief 平滑动画还原到完整频率范围（m_fullFreqLower ~ m_fullFreqUpper）
     *
     * 右键还原手势选择"还原"后调用，300ms OutCubic 缓出动画。
     */
    void animateRestoreFullRange();

    /**
     * @brief 绘制鼠标跟随游标
     */
    void drawTracer(QPainter *painter) const;

    /**
     * @brief 根据频率获取最近 bin 的幅值
     */
    bool spectrumValueAt(double freq, double *value) const;

    /**
     * @brief 创建右上角悬浮工具栏按钮（最大保持/平均/标签/网格/清理）
     */
    void setupToolButtons();

    /**
     * @brief 更新单个按钮的选中/未选中样式
     */
    void updateToolButtonStyle(int index);

    /**
     * @brief 根据鼠标像素位置更新右上角按钮栏的显隐（不再依赖象限判断）
     *
     * 显示区域 = m_buttonBar 控件范围，高度方向扩展为按钮高度的三倍；
     * 鼠标位于该区域内时显示按钮栏，否则隐藏。
     */
    void updateToolBarVisibility();

private slots:
    /**
     * @brief X 轴范围变化时重绘，确保缩放时从完整数据重采样
     */
    void onVisRangeChanged(const QCPRange &newRange);

    /**
     * @brief 最大保持按钮选中状态发生改变
     */
    void onBtnMaxHoldToggled(bool checked);

    /**
     * @brief 平均按钮选中状态发生改变
     */
    void onBtnAvgToggled(bool checked);

    /**
     * @brief 标签按钮选中状态发生改变
     */
    void onBtnLabelToggled(bool checked);

    /**
     * @brief 网格按钮选中状态发生改变
     */
    void onBtnGridToggled(bool checked);

private:
    QCPGraph *m_graph        = nullptr;   ///< 频谱折线
    QPen      m_linePen;                  ///< 谱线画笔
    QCPGraph *m_graphMaxHold = nullptr;   ///< 最大保持谱线
    QPen      m_maxHoldPen;               ///< 最大保持画笔
    QCPGraph *m_graphAvg      = nullptr;  ///< 平均谱线
    QPen      m_avgPen;                   ///< 平均画笔
    QCPGraph *m_graphTfMaxHold = nullptr; ///< 瀑布图最大谱线（灰色）
    QPen      m_tfMaxHoldPen;             ///< 瀑布图最大谱画笔

    int    m_fftlen    = 4096;        ///< 频率格点数
    double m_freqRange = 1000.0;      ///< 总频率跨度 (Hz)，用于缩放时计算可见 bins
    double m_freqStart = 0.0;         ///< 频率起始值 (Hz)，renderSpectrum 中计算 bin 索引时使用
    double m_freqPerBin = m_freqRange / m_fftlen;  ///< 预计算每格频率步进 (Hz)

    int    m_visBinStart = 0;            ///< 当前可见频段起始 bin 索引缓存
    int    m_visBinCount = 0;            ///< 当前可见频段 bin 数缓存

    QVector<float>  m_lastSpectrum;   ///< 最近一帧完整频谱副本
    QVector<float>  m_maxHold;        ///< 最大保持渲染缓存
    QVector<float>  m_avgData;        ///< 平均渲染缓存
    QVector<float>  m_TfMaxHold;      ///< 瀑布图滑动窗口最大谱渲染缓存
    QLinearGradient m_bgGradient;      ///< 原始轴矩形渐变（网格切换时恢复用）

    QVector<HQMark>  m_marks;                    ///< 频谱标记框列表（预分配 HQ_SPECTRUM_MARK_CAPACITY 个，不足时自动扩容）
    HQMark           m_offlineMark;              ///< 离线标记框（列表点击但在当前帧未检测到时使用）
    bool             m_offlineMarkActive = false;///< 离线标记框是否激活（有有效数据时绘制）
    int              m_markCount = 0;            ///< 当前有效的标记数量（≤ m_marks.size()），clearMarks 仅将此置零
    int              m_hoveredMarkIndex = -1;    ///< 当前鼠标悬浮的活动标记索引（-1 = 无悬浮），用于绘制 tooltip
    bool             m_hoverOffline = false;     ///< 当前鼠标是否悬浮在离线标记框上
    QPoint           m_lastMousePos;             ///< 最近一次鼠标位置（像素坐标），用于 tooltip 定位
    bool             m_marksVisible = true;      ///< 标记框是否可见（随网格按钮切换）
    bool             m_tracerEnabled = true;     ///< 频谱游标总开关
    bool             m_tracerVisible = false;    ///< 当前是否绘制游标
    QPoint           m_tracerPos;                ///< 游标鼠标像素位置

    QFrame          *m_buttonBar    = nullptr;   ///< 按钮容器
    int              m_toolBarHeight = 0;        ///< 按钮栏高度（setupToolButtons 时记录，用于像素判断）
    QPushButton     *m_btnMaxHold  = nullptr;    ///< 最大保持
    QPushButton     *m_btnAvg      = nullptr;    ///< 平均
    QPushButton     *m_btnLabel    = nullptr;    ///< 标签
    QPushButton     *m_btnGrid     = nullptr;    ///< 网格
    QPushButton     *m_btnClear    = nullptr;    ///< 清理
    QVector<QPushButton*> m_toolBtns;            ///< 工具栏按钮集合（0-最大保持,1-平均,2-标签,3-网格,4-清理）

    // ========================================================================
    // 拖动选择频率范围
    // ========================================================================
    bool    m_mousePressed  = false;     ///< 鼠标是否在数据区空白处按下（左键）
    bool    m_isDragging    = false;     ///< 是否正在拖动选择频率范围
    QPoint  m_dragStartPos;             ///< 拖动起始像素坐标
    QPoint  m_dragCurrentPos;           ///< 拖动当前像素坐标
    QElapsedTimer m_pressTimer;          ///< 最近移动计时器（每次移动时 restart，松开须距上次移动超过 200ms 且位置稳定才执行缩放/还原）
    double  m_fullFreqLower = 0.0;      ///< 完整频率范围下限（用于从右向左拖动时还原）
    double  m_fullFreqUpper = 0.0;      ///< 完整频率范围上限（用于从右向左拖动时还原）
    QVariantAnimation *m_dragAnim = nullptr; ///< 拖动缩放动画（300ms 缓出），复用避免频繁 new/delete

    // ========================================================================
    // 右键还原手势（拖动立即激活，松开超过 200ms 后在目标位置弹出 还原/取消 菜单）
    // ========================================================================
    bool    m_rightPressed      = false;    ///< 鼠标是否在数据区空白处按下（右键）
    bool    m_rightDragging     = false;    ///< 是否正在执行右键还原手势拖动
    QPoint  m_rightDragStartPos;            ///< 右键手势起始像素坐标
    QPoint  m_rightDragCurrentPos;          ///< 右键手势当前像素坐标
    QVector<QPoint> m_rightTrajectory;      ///< 右键手势轨迹点序列（沿鼠标实际移动路径）
    QCursor m_brushCursor;                  ///< 右键手势进行中的画笔光标
};
