#pragma once

#include "qcustomplot.h"
#include <QPointF>
#include <QString>
#include <QFont>
#include <QColor>

// ============================================================================
// 枚举定义
// ============================================================================

/**
 * @brief 鼠标在坐标轴区域的方位枚举
 *
 * 用于描述鼠标相对于坐标轴绘图区的位置，便于在时频图/频谱图中
 * 根据鼠标位置实现不同的交互行为（如拖拽缩放区域、调整轴范围等）。
 */
enum class AxisZone {
    AtXTop,      ///< 鼠标在 X 轴顶部区域（轴矩形的上方边缘带）
    AtXBottom,   ///< 鼠标在 X 轴底部区域（轴矩形的下方边缘带）
    AtYLeft,     ///< 鼠标在 Y 轴左侧区域（轴矩形的左方边缘带）
    AtYRight,    ///< 鼠标在 Y 轴右侧区域（轴矩形的右方边缘带）
    AtCenter,    ///< 鼠标在绘图中心区域（轴矩形内部，远离各边缘）
    AtOutside    ///< 鼠标在坐标轴区域之外
};

/**
 * @brief 鼠标在数据坐标系中的四象限位置枚举
 *
 * 根据当前可视范围的中心点将绘图区域分为四个象限。
 * 中心点随坐标轴范围动态变化：(centerX, centerY) = ((lower+upper)/2)。
 * 用于快速判断数据点相对于视图中心的位置特征。
 */
enum class Quadrant {
    Q1,          ///< 第一象限 (x > centerX, y > centerY)，右上区域
    Q2,          ///< 第二象限 (x < centerX, y > centerY)，左上区域
    Q3,          ///< 第三象限 (x < centerX, y < centerY)，左下区域
    Q4,          ///< 第四象限 (x > centerX, y < centerY)，右下区域
    Origin,      ///< 视图中心点附近 (x ≈ centerX, y ≈ centerY)
    OnXAxis,     ///< 在水平中心线上 (y ≈ centerY, x ≠ centerX)
    OnYAxis,     ///< 在垂直中心线上 (x ≈ centerX, y ≠ centerY)
    Outside      ///< 在坐标轴范围之外
};


// ============================================================================
// SpecPlotBase 类声明
// ============================================================================

/**
 * @brief 频率轴刻度生成器（自动将 Hz 转换为 MHz 显示）
 *
 * 重写 QCPAxisTicker::getTickLabel，将内部 Hz 单位的坐标值除以 1e6
 * 转换为 MHz 显示，并在刻度标签后附加 " MHz" 单位后缀。
 */
class FrequencyMHzTicker : public QCPAxisTicker
{
public:
    void setMaxTick(double rangeTick) {m_rangeTick = rangeTick;}

protected:
    /**
     * @brief 生成刻度标签文本
     * @param tick      刻度位置（内部 Hz 值）
     * @param locale    区域设置
     * @param formatChar 数字格式字符
     * @param precision  小数精度
     * @return 格式化后的标签文本，如 "100GHz"
     */
    QString getTickLabel(double tick, const QLocale &locale, QChar formatChar, int precision) override
    {
        // 强制使用 'f' 固定小数点格式，避免缩放时出现科学计数法（如 1e-3）
        Q_UNUSED(formatChar);
        if (m_rangeTick > 1e9)
        {
            return QCPAxisTicker::getTickLabel(tick / 1e9, locale, QChar('f'), 3)
               + QStringLiteral("GHz");
        }
        else if (m_rangeTick > 1e6)
        {
            return QCPAxisTicker::getTickLabel(tick / 1e6, locale, QChar('f'), 3)
               + QStringLiteral("MHz");
        }
        else
        {
            return QCPAxisTicker::getTickLabel(tick / 1e3, locale, QChar('f'), 3)
               + QStringLiteral("KHz");
        }
    }

private:
    double m_rangeTick = 0.0;
};


/**
 * @brief 时频图/频谱图的高效率基类
 *
 * 基于 QCustomPlot 封装了频谱分析和时频分析所需的通用功能：
 * - 鼠标位置检测（轴区域方位 + 数据象限）
 * - 像素坐标与数据坐标的高效双向转换（带缓存）
 * - 坐标轴初始化、范围设置及缩放限制
 *
 * 子类（如 SpectrumPlot、WaterfallPlot）只需关注数据绑定和
 * 可视化样式的实现，通用的坐标管理和鼠标交互由此基类提供。
 */
class SpecPlotBase : public QCustomPlot
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param parent 父控件指针
     *
     * 自动调用 setupDefaultAxes() 初始化默认坐标轴。
     */
    explicit SpecPlotBase(QWidget *parent = nullptr);

    /** @brief 析构函数 */
    ~SpecPlotBase() override = default;

    // ========================================================================
    // 位置检测接口
    // ========================================================================

    /**
     * @brief 根据像素位置获取鼠标在坐标轴区域的方位
     * @param pixelPos 鼠标在控件坐标系中的像素位置
     * @return 轴区域方位枚举值
     *
     * 通过 coordToPixel 将数据范围边界转换为像素坐标，得到数据区的像素矩形。
     * 鼠标在矩形内返回 AtCenter，在矩形外则取到四条边距离最近的一侧。
     */
    AxisZone getAxisZone(const QPointF &pixelPos) const;

    /**
     * @brief 根据像素位置获取鼠标在数据坐标系中的象限
     * @param pixelPos 鼠标在控件坐标系中的像素位置
     * @return 四象限位置枚举值
     *
     * 先将像素坐标转换为数据坐标，再以当前可视范围的中心点
     * (centerX, centerY) = ((xLower+xUpper)/2, (yLower+yUpper)/2) 为原点
     * 判定所属象限。使用 epsilon 容差处理浮点零值判定。
     */
    Quadrant getQuadrant(const QPointF &pixelPos) const;

    /**
     * @brief 将像素坐标转换为数据坐标
     * @param pixelPos 控件坐标系中的像素位置
     * @return 数据坐标系中的坐标 (x, y)
     *
     * 分别使用 xAxis 和 yAxis 的 pixelToCoord() 进行转换。
     * 结果会被缓存以提高连续查询的效率。
     */
    QPointF getDataCoord(const QPointF &pixelPos) const;

    /**
     * @brief 将数据坐标转换为像素坐标
     * @param dataCoord 数据坐标系中的坐标 (x, y)
     * @return 控件坐标系中的像素位置
     *
     * 分别使用 xAxis 和 yAxis 的 coordToPixel() 进行转换。
     * 结果会被缓存以提高连续查询的效率。
     */
    QPointF getPixelPos(const QPointF &dataCoord) const;

    // ========================================================================
    // 坐标轴设置接口
    // ========================================================================

    /**
     * @brief 初始化默认坐标轴配置
     *
     * 设置 xAxis 为底部水平轴、yAxis 为左侧垂直轴，
     * 并将它们注册为轴矩形的默认拖拽/缩放轴。
     * 子类可在调用此方法后进一步自定义轴样式。
     */
    void setupDefaultAxes();

    /**
     * @brief 设置 X 轴显示范围
     * @param lower 范围下界
     * @param upper 范围上界
     *
     * 若已通过 setXRangeLimit() 设置了范围限制，则自动 clamp 到限制范围内。
     * 调用后会触发 replot()。
     */
    void setXRange(double lower, double upper);

    /**
     * @brief 设置 Y 轴显示范围
     * @param lower 范围下界
     * @param upper 范围上界
     *
     * 若已通过 setYRangeLimit() 设置了范围限制，则自动 clamp 到限制范围内。
     * 调用后会触发 replot()。
     */
    void setYRange(double lower, double upper);

    /**
     * @brief 设置 X 轴范围限制（缩放/拖拽时不可超越的边界）
     * @param min 最小允许值
     * @param max 最大允许值
     *
     * 设置后，用户通过鼠标缩放或拖拽无法让 X 轴范围超出此限制。
     * 若当前范围已超出限制，会立即 clamp 并 replot()。
     */
    void setXRangeLimit(double min, double max);

    /**
     * @brief 设置 Y 轴范围限制（缩放/拖拽时不可超越的边界）
     * @param min 最小允许值
     * @param max 最大允许值
     *
     * 设置后，用户通过鼠标缩放或拖拽无法让 Y 轴范围超出此限制。
     * 若当前范围已超出限制，会立即 clamp 并 replot()。
     */
    void setYRangeLimit(double min, double max);

    /**
     * @brief 清除 X 轴范围限制
     */
    void clearXRangeLimit();

    /**
     * @brief 清除 Y 轴范围限制
     */
    void clearYRangeLimit();

    /**
     * @brief 设置 X 轴标签文本
     * @param label 标签文本
     */
    void setXLabel(const QString &label);

    /**
     * @brief 设置 Y 轴标签文本
     * @param label 标签文本
     */
    void setYLabel(const QString &label);

    // ========================================================================
    // 刻度标签显隐
    // ========================================================================

    /**
     * @brief 设置 X 轴刻度标签是否可见
     * @param visible true=显示，false=隐藏
     */
    void setXTickLabelsVisible(bool visible);

    /** @brief 获取 X 轴刻度标签是否可见 */
    bool isXTickLabelsVisible() const;

    /**
     * @brief 设置 Y 轴刻度标签是否可见
     * @param visible true=显示，false=隐藏
     */
    void setYTickLabelsVisible(bool visible);

    /** @brief 获取 Y 轴刻度标签是否可见 */
    bool isYTickLabelsVisible() const;

    /**
     * @brief 统一设置所有坐标轴的字体
     * @param font 轴标签和刻度标签使用的字体
     *
     * 同时应用到 xAxis 和 yAxis 的标签字体和刻度标签字体。
     */
    void setAxisFont(const QFont &font);

    /**
     * @brief 统一设置所有坐标轴的颜色
     * @param color 轴线条、刻度线和标签的颜色
     *
     * 同时应用到 xAxis 和 yAxis 的基色、刻度颜色、子刻度颜色和标签颜色。
     */
    void setAxisColor(const QColor &color);

    /**
     * @brief 统一设置所有坐标轴的刻度标签文字颜色
     * @param color 刻度标签文字颜色
     *
     * 同时应用到 xAxis 和 yAxis 的刻度标签颜色。
     */
    void setTickLabelColor(const QColor &color);

    /**
     * @brief 统一设置所有坐标轴的标签文字颜色
     * @param color 轴标签文字颜色（如"频率 (Hz)"、"时间 (s)"的颜色）
     *
     * 同时应用到 xAxis 和 yAxis 的标签颜色。
     */
    void setLabelColor(const QColor &color);

    // ========================================================================
    // 固定 Y 轴标签（ptViewportRatio 锚定 + 旋转 90°）
    // ========================================================================

    /**
     * @brief 用固定视口位置的旋转文字替代内置 Y 轴标签
     * @param text 标签文字（如"时间 (s)"、"功率 (dB)"）
     *
     * 隐藏 yAxis 的内置标签，创建 QCPItemText 以 ptViewportRatio
     * 锚定在左上角，文字旋转 90°（竖向）。多个子类调用此方法后，
     * 因锚点比例一致，标签位置完全对齐。
     */
    void setupFixedYLabel(const QString &text);

signals:
    /**
     * @brief 鼠标在坐标轴区域内的方位发生变化时发射
     * @param zone 新的轴区域方位
     *
     * 仅在 zone 值实际变化时发射（已去重）。
     */
    void axisZoneChanged(AxisZone zone);

    /**
     * @brief 鼠标在数据坐标系中的象限发生变化时发射
     * @param quadrant 新的象限位置
     *
     * 仅在 quadrant 值实际变化时发射（已去重）。
     */
    void quadrantChanged(Quadrant quadrant);

    /**
     * @brief 鼠标对应的数据坐标发生变化时发射
     * @param coord 新的数据坐标 (x, y)
     *
     * 仅在 coord 值实际变化时发射（已去重）。
     */
    void dataCoordChanged(const QPointF &coord);

protected:
    /**
     * @brief 鼠标按下事件重写
     *
     * 记录按下时的轴区域方位。若按下位置在数据绘图区 (AtCenter) 或区域外 (AtOutside)，
     * 则临时禁用 iRangeDrag 交互，确保**拖动操作仅在轴刻度区域（左上右下）生效**。
     */
    void mousePressEvent(QMouseEvent *event) override;

    /**
     * @brief 鼠标移动事件重写
     *
     * 在保留 QCustomPlot 原有拖拽/缩放行为的基础上，
     * 检测当前 AxisZone、Quadrant 和 DataCoord 并发射相应信号。
     */
    void mouseMoveEvent(QMouseEvent *event) override;

    /**
     * @brief 鼠标释放事件重写
     *
     * 恢复在 mousePressEvent 中可能被临时禁用的 iRangeDrag 交互。
     */
    void mouseReleaseEvent(QMouseEvent *event) override;

    /**
     * @brief 滚轮事件重写
     *
     * 根据鼠标所在轴区域智能缩放单个轴：在 X 轴刻度区只缩放 X，
     * 在 Y 轴刻度区只缩放 Y，在数据区则同时缩放两轴。
     */
    void wheelEvent(QWheelEvent *event) override;

    /**
     * @brief 根据轴区域更新悬停光标样式，子类可重写自定义绘图区光标
     * @param zone    当前鼠标所处的轴区域
     * @param mousePos 鼠标在控件坐标系中的位置
     */
    virtual void updateHoverCursor(AxisZone zone, const QPoint &mousePos);

private slots:
    /**
     * @brief X 轴范围变更时自动 clamp 到限制范围内
     *
     * 连接 xAxis::rangeChanged 信号。在 rangeChanged 信号阶段直接拦截越界，
     * 确保任何 replot 之前范围已被修正，彻底杜绝越界画面闪烁。
     */
    void onXRangeChanged(const QCPRange &newRange);

    /**
     * @brief Y 轴范围变更时自动 clamp 到限制范围内
     *
     * 连接 yAxis::rangeChanged 信号，与 onXRangeChanged 相同的即时拦截机制。
     */
    void onYRangeChanged(const QCPRange &newRange);

    /**
     * @brief 强制执行范围限制（供 setXRangeLimit / setYRangeLimit 等公开 API 调用）
     */
    void enforceRangeLimits();

private:
    /**
     * @brief 使所有缓存失效
     *
     * 在轴范围发生变化时调用，确保下次查询重新计算而非返回过期缓存。
     */
    void invalidateCache() const;

    /**
     * @brief 同步固定 Y 轴标签的颜色和字体到当前 yAxis 配置
     *
     * 在主题切换或标签首次创建时调用。
     */
    void applyYLabelTheme();

private:
    // ========================================================================
    // 缓存结构（mutable 允许在 const 查询方法中更新）
    // ========================================================================
    struct Cache {
        bool     valid = false;              ///< 缓存是否有效

        // AxisZone 缓存
        QPointF  lastAxisZonePixelPos;       ///< 上次查询 AxisZone 的像素位置
        AxisZone lastAxisZone = AxisZone::AtOutside; ///< 上次查询 AxisZone 的结果

        // Quadrant 缓存
        QPointF  lastQuadPixelPos;           ///< 上次查询 Quadrant 的像素位置
        Quadrant lastQuadrant = Quadrant::Outside; ///< 上次查询 Quadrant 的结果

        // DataCoord 缓存
        QPointF  lastDCPixelPos;             ///< 上次查询 DataCoord 的像素位置
        QPointF  lastDCResult;               ///< 上次查询 DataCoord 的结果

        // PixelPos 缓存
        QPointF  lastPPDataCoord;            ///< 上次查询 PixelPos 的数据坐标
        QPointF  lastPPResult;               ///< 上次查询 PixelPos 的结果
    };

private:
    mutable Cache m_cache;

    // ========================================================================
    // 范围限制成员
    // ========================================================================
    bool   m_hasXLimit = false;              ///< 是否已设置 X 轴范围限制
    bool   m_hasYLimit = false;              ///< 是否已设置 Y 轴范围限制
    double m_xLimitMin = 0.0;                ///< X 轴范围最小允许值
    double m_xLimitMax = 0.0;                ///< X 轴范围最大允许值
    double m_yLimitMin = 0.0;                ///< Y 轴范围最小允许值
    double m_yLimitMax = 0.0;                ///< Y 轴范围最大允许值

    // ========================================================================
    // 信号去重状态
    // ========================================================================
    AxisZone m_lastEmittedZone = AxisZone::AtOutside;     ///< 上次发射的 AxisZone 值
    Quadrant m_lastEmittedQuadrant = Quadrant::Outside;   ///< 上次发射的 Quadrant 值
    QPointF  m_lastEmittedCoord;                          ///< 上次发射的数据坐标值

    // ========================================================================
    // 拖拽限制
    // ========================================================================
    AxisZone m_dragPressZone = AxisZone::AtOutside;       ///< 鼠标按下时的轴区域，用于判定拖拽行为
    bool     m_dragWasDisabled = false;                   ///< 是否在按下时临时禁用了 iRangeDrag
    bool     m_dragAxesModified = false;                  ///< 是否在按下时临时修改了拖拽轴配置

    // ========================================================================
    // 范围限制递归防护
    // ========================================================================
    bool     m_enforcingXLimits = false;                  ///< 防止 onXRangeChanged 中 setRange 导致递归
    bool     m_enforcingYLimits = false;                  ///< 防止 onYRangeChanged 中 setRange 导致递归

    // ========================================================================
    // 固定 Y 轴标签
    // ========================================================================
    QCPItemText *m_yLabelItem = nullptr;                  ///< 固定视口位置的 Y 轴旋转标签

    QSharedPointer<FrequencyMHzTicker> m_xTickLabels;     ///< x轴刻度值
};
