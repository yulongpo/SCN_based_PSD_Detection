#pragma once

#include <QString>
#include <QColor>
#include <QVector>
#include <QPoint>
#include <QtGlobal>

class QPainter;
class QCPAxisRect;
class QCPAxis;

// 前向声明象限枚举（定义在 SpecPlotBase.h 中，底层类型 int）
enum class Quadrant : int;

/**
 * @brief 频谱标记框样式枚举
 */
enum class HQMarkStyle
{
    Normal = 0,  ///< 正常样式，颜色 (6, 228, 233)
    Error,       ///< 错误样式，颜色 (230, 62, 62)
    Warning,     ///< 警告样式，颜色 (232, 92, 33)
    OffLine      ///< 离线样式，颜色 (128, 128, 128) 灰色，用于列表手动选中但当前帧未检测到的信号
};

/**
 * @brief 频谱标记框，用于在频谱图上绘制标记区域
 *
 * 每个标记框定义一段起始-截止频率范围，框体贯穿坐标轴顶部到底部，
 * 顶部中心显示 CF/BW 文字和朝下的三角形指示符。
 * 支持 Normal、Error、Warning 和 OffLine 四种样式，颜色由枚举类型决定。
 */
class HQMark
{
public:
    /**
     * @brief 构造标记框
     * @param freqStart 起始频率（数据坐标 Hz）
     * @param freqStop  截止频率（数据坐标 Hz）
     * @param style     标记样式
     * @param cfText    CF 显示文本
     * @param bwText    BW 显示文本
     */
    explicit HQMark(int64_t id = 0,
                    double freqStart = 0.0,
                    double freqStop = 120.0,
                    HQMarkStyle style = HQMarkStyle::Normal,
                    const QString &cfText = QStringLiteral("1.1G"),
                    const QString &bwText = QStringLiteral("10k"));

    /**
     * @brief 绘制标记框（仅绘制有色矩形框体，不绘制顶部三角形和文字）
     * @param painter   QPainter 对象
     * @param axisRect  轴矩形（像素坐标）
     * @param xAxis     X 轴，用于频率 → 像素坐标转换
     * @param yAxis     Y 轴，获取轴矩形顶部/底部
     */
    void draw(QPainter *painter,
              const QCPAxisRect *axisRect,
              const QCPAxis *xAxis,
              const QCPAxis *yAxis) const;

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

    /** @brief 设置ID */
    void setId(int64_t id) { m_id = id; }
    /** @brief 获取ID */
    int64_t id() const { return m_id; }
    /** @brief 设置起始频率 */
    void setFreqStart(double freq) { m_freqStart = freq; }
    /** @brief 获取起始频率 */
    double freqStart() const { return m_freqStart; }
    /** @brief 设置截止频率 */
    void setFreqStop(double freq) { m_freqStop = freq; }
    /** @brief 获取截止频率 */
    double freqStop() const { return m_freqStop; }

    /** @brief 设置标记样式 */
    void setStyle(HQMarkStyle style) { m_style = style; }
    /** @brief 获取标记样式 */
    HQMarkStyle style() const { return m_style; }

    /** @brief 设置 CF 文本 */
    void setCfText(const QString &text) { m_cfText = text; }
    /** @brief 设置 BW 文本 */
    void setBwText(const QString &text) { m_bwText = text; }

    /** @brief 设置选中状态 */
    void setSelected(bool selected) { m_selected = selected; }
    /** @brief 获取选中状态 */
    bool isSelected() const { return m_selected; }

    /** @brief 设置 UI 过渡透明度（0~1）。 */
    void setOpacity(double opacity) { m_opacity = qBound(0.0, opacity, 1.0); }
    /** @brief 获取 UI 过渡透明度。 */
    double opacity() const { return m_opacity; }

    /**
     * @brief 检测像素点是否在标记框范围内
     * @param pos      控件坐标系的像素点
     * @param axisRect 轴矩形
     * @param xAxis    X 轴，用于数据→像素转换
     * @return true 点在标记框内
     */
    bool containsPoint(const QPoint &pos,
                       const QCPAxisRect *axisRect,
                       const QCPAxis *xAxis) const;

private:
    /** @brief 获取样式对应的颜色 */
    QColor styleColor() const;

    int64_t     m_id;          ///< ID
    double      m_freqStart;   ///< 起始频率（数据坐标 Hz）
    double      m_freqStop;    ///< 截止频率（数据坐标 Hz）
    HQMarkStyle m_style;       ///< 标记样式
    QString     m_cfText;      ///< CF 标签值
    QString     m_bwText;      ///< BW 标签值
    bool        m_selected = false;  ///< 是否选中
    double      m_opacity = 1.0;     ///< 仅用于频谱显示的淡入淡出，不影响业务数据

    static constexpr int kTriMinBase = 6;         ///< 三角形最小边长基准（设备 px）
    static constexpr int kTriMaxBase = 14;        ///< 三角形最大边长基准（设备 px）
};
