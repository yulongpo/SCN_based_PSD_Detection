#pragma once

#include <QString>
#include <QColor>
#include <QVector>
#include <QPoint>
#include <cstdint>

class QPainter;
class QCPAxisRect;
class QCPAxis;

// 前向声明象限枚举（定义在 SpecPlotBase.h 中，底层类型 int）
enum class Quadrant : int;

/**
 * @brief 频谱标记框样式枚举
 */
enum class HQTFMarkStyle
{
    Normal = 0,  ///< 正常样式，颜色 (6, 228, 233)
    Error,       ///< 错误样式，颜色 (230, 62, 62)
    Warning,     ///< 警告样式，颜色 (232, 92, 33)
    OffLine      ///< 离线样式，颜色 (128, 128, 128) 灰色，用于列表手动选中但当前帧未检测到的信号
};

/**
 * @brief 频谱/时频图标记框
 *
 * 用于在频谱图或时频瀑布图上绘制标记区域。
 *
 * 频谱图模式（仅频率范围）：
 *   框体贯穿坐标轴顶部到底部，标记一个频段。
 *
 * 时频图模式（频率 + 时间范围）：
 *   通过 setTimeRangeMs() 设置起止时间戳后，框体仅在指定的时间段内绘制。
 *   时间戳对应 HQTfwaterfall::m_timeBuf 中存储的毫秒值。
 *
 * 四种样式 Normal / Error / Warning / OffLine 由枚举类型决定颜色。
 */
class HQTFMark
{
public:
    /**
     * @brief 构造标记框
     * @param id        标记唯一ID
     * @param freqStart 起始频率（数据坐标 Hz）
     * @param freqStop  截止频率（数据坐标 Hz）
     * @param style     标记样式
     * @param cfText    CF 显示文本
     * @param bwText    BW 显示文本
     */
    explicit HQTFMark(int64_t id = 0,
                    double freqStart = 0.0,
                    double freqStop = 120.0,
                    HQTFMarkStyle style = HQTFMarkStyle::Normal,
                    const QString &cfText = QStringLiteral("1.1G"),
                    const QString &bwText = QStringLiteral("10k"));

    // ========================================================================
    // 绘制接口
    // ========================================================================

    /**
     * @brief 绘制标记框
     * @param painter      QPainter 对象
     * @param axisRect     轴矩形（像素坐标）
     * @param xAxis        X 轴，用于频率 → 像素坐标转换
     * @param yAxis        Y 轴（时频图模式下用于坐标裁剪，频谱图模式下忽略）
     * @param yPixelTop    时频图模式下的 Y 轴顶部像素坐标（-1 表示贯穿整个高度）
     * @param yPixelBottom 时频图模式下的 Y 轴底部像素坐标（-1 表示贯穿整个高度）
     *
     * 频谱图模式（yPixelTop < 0 或 yPixelBottom < 0）：
     *   框体从 axisRect 顶部贯穿到底部，标记整个频段。
     *
     * 时频图模式（yPixelTop >= 0 且 yPixelBottom >= 0）：
     *   框体仅在 [yPixelTop, yPixelBottom] 范围内绘制，标记特定时间段内的频率。
     */
    void draw(QPainter *painter,
              const QCPAxisRect *axisRect,
              const QCPAxis *xAxis,
              const QCPAxis *yAxis,
              double yPixelTop = -1.0,
              double yPixelBottom = -1.0) const;

    /**
     * @brief 在鼠标位置附近绘制悬浮提示文字（CF / BW 信息）
     * @param painter   QPainter 对象
     * @param mousePos  鼠标在控件坐标系中的像素位置
     * @param quadrant  鼠标在数据坐标系中所处的象限（决定文字绘制方位）
     *
     * 文字绘制策略（使文字始终远离视图中心，避免被裁切）：
     * - Q1（右上）：文字在鼠标左下方
     * - Q2（左上）：文字在鼠标右下方
     * - Q3（左下）：文字在鼠标右上方
     * - Q4（右下）：文字在鼠标左上方
     */
    void drawTooltip(QPainter *painter, const QPointF &mousePos, Quadrant quadrant) const;

    // ========================================================================
    // 命中检测
    // ========================================================================

    /**
     * @brief 检测像素点是否在标记框范围内
     * @param pos          控件坐标系的像素点
     * @param axisRect     轴矩形
     * @param xAxis        X 轴，用于数据→像素转换
     * @param yPixelTop    时频图模式下的 Y 轴顶部像素坐标（-1 表示贯穿整个高度）
     * @param yPixelBottom 时频图模式下的 Y 轴底部像素坐标（-1 表示贯穿整个高度）
     * @return true 点在标记框内
     */
    bool containsPoint(const QPoint &pos,
                       const QCPAxisRect *axisRect,
                       const QCPAxis *xAxis,
                       double yPixelTop,
                       double yPixelBottom) const;

    // ========================================================================
    // 属性访问器
    // ========================================================================

    /** @brief 设置ID */
    void setId(int64_t id) { m_id = id; }
    /** @brief 获取ID */
    int64_t id() const { return m_id; }

    /** @brief 设置起始频率（Hz） */
    void setFreqStart(double freq) { m_freqStart = freq; }
    /** @brief 获取起始频率（Hz） */
    double freqStart() const { return m_freqStart; }
    /** @brief 设置截止频率（Hz） */
    void setFreqStop(double freq) { m_freqStop = freq; }
    /** @brief 获取截止频率（Hz） */
    double freqStop() const { return m_freqStop; }

    /** @brief 设置标记样式 */
    void setStyle(HQTFMarkStyle style) { m_style = style; }
    /** @brief 获取标记样式 */
    HQTFMarkStyle style() const { return m_style; }

    /** @brief 设置 CF 文本 */
    void setCfText(const QString &text) { m_cfText = text; }
    /** @brief 设置 BW 文本 */
    void setBwText(const QString &text) { m_bwText = text; }

    /** @brief 设置选中状态 */
    void setSelected(bool selected) { m_selected = selected; }
    /** @brief 获取选中状态 */
    bool isSelected() const { return m_selected; }

    // ========================================================================
    // 时频图时间范围（对应 m_timeBuf 中的毫秒时间戳）
    // ========================================================================

    /**
     * @brief 设置时间范围（毫秒时间戳，对应 HQTfwaterfall::m_timeBuf 中的值）
     * @param startMs 起始时间戳（ms，epoch 毫秒）
     * @param stopMs  截止时间戳（ms，epoch 毫秒）
     * @note  设置后，draw() 仅在 [startMs, stopMs] 对应的时间段内绘制标记框。
     *         调用 clearTimeRange() 可恢复为贯穿整个 Y 轴。
     */
    void setTimeRangeMs(int64_t startMs, int64_t stopMs)
    {
        m_timeStartMs   = startMs;
        m_timeStopMs    = stopMs;
        m_hasTimeRange  = (startMs > 0 || stopMs > 0);
    }

    /** @brief 清除时间范围（恢复为贯穿整个 Y 轴的频谱图模式） */
    void clearTimeRange() { m_hasTimeRange = false; m_timeStartMs = 0; m_timeStopMs = 0; }

    /** @brief 获取起始时间戳（ms） */
    int64_t timeStartMs() const { return m_timeStartMs; }
    /** @brief 获取截止时间戳（ms） */
    int64_t timeStopMs() const { return m_timeStopMs; }
    /** @brief 是否已设置时间范围 */
    bool hasTimeRange() const { return m_hasTimeRange; }

private:
    /** @brief 获取样式对应的颜色 */
    QColor styleColor() const;

    // ---- 频率范围 ----
    int64_t     m_id;          ///< ID
    double      m_freqStart;   ///< 起始频率（数据坐标 Hz）
    double      m_freqStop;    ///< 截止频率（数据坐标 Hz）

    // ---- 样式 ----
    HQTFMarkStyle m_style;       ///< 标记样式
    QString     m_cfText;      ///< CF 标签值
    QString     m_bwText;      ///< BW 标签值
    bool        m_selected = false;  ///< 是否选中

    // ---- 时间范围（时频图模式） ----
    int64_t m_timeStartMs  = 0;     ///< 起始时间戳（ms，对应 m_timeBuf 中的值）
    int64_t m_timeStopMs   = 0;     ///< 截止时间戳（ms，对应 m_timeBuf 中的值）
    bool    m_hasTimeRange = false; ///< 是否启用时间范围约束

    // ---- 三角形尺寸常量 ----
    static constexpr int kTriMinBase = 6;         ///< 三角形最小边长基准（设备 px）
    static constexpr int kTriMaxBase = 14;        ///< 三角形最大边长基准（设备 px）
};
