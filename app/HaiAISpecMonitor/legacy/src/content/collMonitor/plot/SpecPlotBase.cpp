#include "SpecPlotBase.h"
#include <QMouseEvent>
#include <QWheelEvent>
#include <QtMath>

#include "comm/CommonMacros.h"
#include "comm/ScreenScale.h"

// ============================================================================
// 构造与析构
// ============================================================================

SpecPlotBase::SpecPlotBase(QWidget *parent)
    : QCustomPlot(parent), m_xTickLabels(QSharedPointer<FrequencyMHzTicker>(new FrequencyMHzTicker))
{
    // 初始化默认坐标轴
    setupDefaultAxes();

    // 轴范围变化时即时 clamp 到限制范围，并同步使缓存失效
    connect(xAxis, QOverload<const QCPRange &>::of(&QCPAxis::rangeChanged),
            this, &SpecPlotBase::onXRangeChanged);
    connect(yAxis, QOverload<const QCPRange &>::of(&QCPAxis::rangeChanged),
            this, &SpecPlotBase::onYRangeChanged);
}

// ============================================================================
// 位置检测 — AxisZone
// ============================================================================

AxisZone SpecPlotBase::getAxisZone(const QPointF &pixelPos) const
{
    // --- 缓存命中检查 ---
    if (m_cache.valid && m_cache.lastAxisZonePixelPos == pixelPos) {
        return m_cache.lastAxisZone;
    }

    // --- 将数据范围边界转换为像素坐标，得到数据区的像素矩形 ---
    const QCPRange xRange = xAxis->range();
    const QCPRange yRange = yAxis->range();

    double xLeft   = xAxis->coordToPixel(xRange.lower);
    double xRight  = xAxis->coordToPixel(xRange.upper);
    double yBottom = yAxis->coordToPixel(yRange.lower);  // 像素 Y 轴向下，lower 对应下方
    double yTop    = yAxis->coordToPixel(yRange.upper);  // upper 对应上方

    // 处理轴翻转：确保 left < right, top < bottom
    if (xLeft > xRight)  qSwap(xLeft, xRight);
    if (yTop  > yBottom) qSwap(yTop, yBottom);

    const double px = pixelPos.x();
    const double py = pixelPos.y();

    AxisZone result;

    // --- 数据区内部 → 中心区域 ---
    if (px >= xLeft && px <= xRight && py >= yTop && py <= yBottom) {
        result = AxisZone::AtCenter;
    }
    else {
        // 以数据区四条边为基准，计算到各边的距离
        const double dTop    = qAbs(py - yTop);
        const double dBottom = qAbs(py - yBottom);
        const double dLeft   = qAbs(px - xLeft);
        const double dRight  = qAbs(px - xRight);

        double minDist = std::numeric_limits<double>::max();

        if (dTop    < minDist) { minDist = dTop;    result = AxisZone::AtXTop; }
        if (dBottom < minDist) { minDist = dBottom; result = AxisZone::AtXBottom; }
        if (dLeft   < minDist && py < yBottom) { minDist = dLeft;   result = AxisZone::AtYLeft; }
        if (dRight  < minDist && py < yBottom) { minDist = dRight;  result = AxisZone::AtYRight; }
    }

    // --- 更新缓存 ---
    m_cache.valid                 = true;
    m_cache.lastAxisZonePixelPos  = pixelPos;
    m_cache.lastAxisZone          = result;

    return result;
}

// ============================================================================
// 位置检测 — Quadrant
// ============================================================================

Quadrant SpecPlotBase::getQuadrant(const QPointF &pixelPos) const
{
    // --- 缓存命中检查 ---
    if (m_cache.valid && m_cache.lastQuadPixelPos == pixelPos) {
        return m_cache.lastQuadrant;
    }

    // --- 像素坐标 → 数据坐标 ---
    const QPointF dataCoord = getDataCoord(pixelPos);
    const double x = dataCoord.x();
    const double y = dataCoord.y();

    // --- 获取当前轴范围 ---
    const QCPRange xRange = xAxis->range();
    const QCPRange yRange = yAxis->range();

    // --- 以可视范围中心点为"原点" ---
    const double centerX = xRange.center();  // (lower + upper) / 2.0
    const double centerY = yRange.center();
    const double dx = x - centerX;
    const double dy = y - centerY;

    // 基于范围的 epsilon（适配大范围坐标轴如 GHz 频率）
    // 至少保证 1e-12 的绝对精度，同时按范围的 1e-9 做相对缩放
    const double xEps = qMax(1e-12, 1e-9 * qMax(1.0, qAbs(xRange.upper - xRange.lower)));
    const double yEps = qMax(1e-12, 1e-9 * qMax(1.0, qAbs(yRange.upper - yRange.lower)));

    // 相对于中心点的"零值"判定
    const bool dxIsZero = qAbs(dx) <= xEps;
    const bool dyIsZero = qAbs(dy) <= yEps;

    Quadrant result;

    // --- 先判定是否在可视范围内 ---
    if (x < xRange.lower - xEps || x > xRange.upper + xEps ||
        y < yRange.lower - yEps || y > yRange.upper + yEps) {
        result = Quadrant::Outside;
    }
    // --- 视图中心 ---
    else if (dxIsZero && dyIsZero) {
        result = Quadrant::Origin;
    }
    // --- 在水平中心线上 ---
    else if (dyIsZero && !dxIsZero) {
        result = Quadrant::OnXAxis;
    }
    // --- 在垂直中心线上 ---
    else if (dxIsZero && !dyIsZero) {
        result = Quadrant::OnYAxis;
    }
    // --- 四象限（相对于视图中心）---
    else if (dx > 0.0 && dy > 0.0) {
        result = Quadrant::Q1;
    }
    else if (dx < 0.0 && dy > 0.0) {
        result = Quadrant::Q2;
    }
    else if (dx < 0.0 && dy < 0.0) {
        result = Quadrant::Q3;
    }
    else { // dx > 0 && dy < 0
        result = Quadrant::Q4;
    }

    // --- 更新缓存 ---
    m_cache.valid            = true;
    m_cache.lastQuadPixelPos = pixelPos;
    m_cache.lastQuadrant     = result;

    return result;
}

// ============================================================================
// 坐标转换
// ============================================================================

QPointF SpecPlotBase::getDataCoord(const QPointF &pixelPos) const
{
    // --- 缓存命中检查 ---
    if (m_cache.valid && m_cache.lastDCPixelPos == pixelPos) {
        return m_cache.lastDCResult;
    }

    // --- 各轴独立转换 ---
    const double x = xAxis->pixelToCoord(pixelPos.x());
    const double y = yAxis->pixelToCoord(pixelPos.y());

    const QPointF result(x, y);

    // --- 更新缓存 ---
    m_cache.valid          = true;
    m_cache.lastDCPixelPos = pixelPos;
    m_cache.lastDCResult   = result;

    return result;
}

QPointF SpecPlotBase::getPixelPos(const QPointF &dataCoord) const
{
    // --- 缓存命中检查 ---
    if (m_cache.valid && m_cache.lastPPDataCoord == dataCoord) {
        return m_cache.lastPPResult;
    }

    // --- 各轴独立转换 ---
    const double px = xAxis->coordToPixel(dataCoord.x());
    const double py = yAxis->coordToPixel(dataCoord.y());

    const QPointF result(px, py);

    // --- 更新缓存 ---
    m_cache.valid           = true;
    m_cache.lastPPDataCoord = dataCoord;
    m_cache.lastPPResult    = result;

    return result;
}

// ============================================================================
// 默认坐标轴初始化
// ============================================================================

void SpecPlotBase::setupDefaultAxes()
{
    QCPAxisRect *ar = axisRect(0);
    if (!ar) {
        return;
    }

    // 将默认坐标轴注册为拖拽和缩放操作的响应轴
    ar->setRangeDragAxes(xAxis, yAxis);
    ar->setRangeZoomAxes(xAxis, yAxis);

    // 设置默认的轴标签
    //xAxis->setLabel(QStringLiteral("频率 (GHz)"));
    yAxis->setLabel(QStringLiteral("幅度 (dB)"));

    xAxis->setTicker(m_xTickLabels);

    // 启用轴标签和刻度标签
    xAxis->setVisible(true);
    yAxis->setVisible(true);

    // 隐藏主刻度线和子刻度线（使用透明画笔，保留边距计算以确保刻度文字不被裁切）
    xAxis->setTickPen(QPen(Qt::NoPen));
    xAxis->setSubTickPen(QPen(Qt::NoPen));
    yAxis->setTickPen(QPen(Qt::NoPen));
    yAxis->setSubTickPen(QPen(Qt::NoPen));

    // 隐藏顶部和右侧辅助轴（无刻度，不参与拖拽交互）
    // 子类如需使用可通过 setVisible(true) 重新启用
    xAxis2->setVisible(false);
    yAxis2->setVisible(false);

    // 设置默认交互：允许拖拽范围和滚轮缩放
    setInteraction(QCP::iRangeDrag, true);
    setInteraction(QCP::iRangeZoom, true);
}

// ============================================================================
// 坐标轴范围设置
// ============================================================================

void SpecPlotBase::setXRange(double lower, double upper)
{
    // 若已设置范围限制，先 clamp
    if (m_hasXLimit) {
        lower = qBound(m_xLimitMin, lower, m_xLimitMax);
        upper = qBound(m_xLimitMin, upper, m_xLimitMax);
    }
    xAxis->setRange(lower, upper);
    invalidateCache();
    replot();
}

void SpecPlotBase::setYRange(double lower, double upper)
{
    // 若已设置范围限制，先 clamp
    if (m_hasYLimit) {
        lower = qBound(m_yLimitMin, lower, m_yLimitMax);
        upper = qBound(m_yLimitMin, upper, m_yLimitMax);
    }
    yAxis->setRange(lower, upper);
    invalidateCache();
    replot();
}

// ============================================================================
// 坐标轴范围限制
// ============================================================================

void SpecPlotBase::setXRangeLimit(double min, double max)
{
    m_hasXLimit = true;
    m_xLimitMin = min;
    m_xLimitMax = max;
    enforceRangeLimits();
}

void SpecPlotBase::setYRangeLimit(double min, double max)
{
    m_hasYLimit = true;
    m_yLimitMin = min;
    m_yLimitMax = max;
    enforceRangeLimits();
}

void SpecPlotBase::clearXRangeLimit()
{
    m_hasXLimit = false;
}

void SpecPlotBase::clearYRangeLimit()
{
    m_hasYLimit = false;
}

// ============================================================================
// 坐标轴标签
// ============================================================================

void SpecPlotBase::setXLabel(const QString &label)
{
    xAxis->setLabel(label);
    replot();
}

void SpecPlotBase::setYLabel(const QString &label)
{
    yAxis->setLabel(label);
    replot();
}

// ============================================================================
// 刻度标签显隐
// ============================================================================

void SpecPlotBase::setXTickLabelsVisible(bool visible)
{
    xAxis->setTickLabels(visible);
    replot();
}

bool SpecPlotBase::isXTickLabelsVisible() const
{
    return xAxis->tickLabels();
}

void SpecPlotBase::setYTickLabelsVisible(bool visible)
{
    yAxis->setTickLabels(visible);
    replot();
}

bool SpecPlotBase::isYTickLabelsVisible() const
{
    return yAxis->tickLabels();
}

// ============================================================================
// 坐标轴样式
// ============================================================================

void SpecPlotBase::setAxisFont(const QFont &font)
{
    // 设置轴标签字体
    xAxis->setLabelFont(font);
    yAxis->setLabelFont(font);

    // 设置刻度标签字体
    xAxis->setTickLabelFont(font);
    yAxis->setTickLabelFont(font);

    replot();
}

void SpecPlotBase::setAxisColor(const QColor &color)
{
    // 设置轴基色（线条）
    xAxis->setBasePen(QPen(color));
    yAxis->setBasePen(QPen(color));

    // 设置刻度线颜色
    xAxis->setTickPen(QPen(color));
    yAxis->setTickPen(QPen(color));

    // 设置子刻度线颜色
    xAxis->setSubTickPen(QPen(color));
    yAxis->setSubTickPen(QPen(color));

    // 设置刻度标签颜色
    xAxis->setTickLabelColor(color);
    yAxis->setTickLabelColor(color);

    // 设置轴标签颜色
    xAxis->setLabelColor(color);
    yAxis->setLabelColor(color);

    replot();
}

void SpecPlotBase::setTickLabelColor(const QColor &color)
{
    // 设置刻度标签文字颜色
    xAxis->setTickLabelColor(color);
    yAxis->setTickLabelColor(color);

    replot();
}

void SpecPlotBase::setLabelColor(const QColor &color)
{
    // 设置轴标签文字颜色（如"频率 (Hz)"、"时间 (s)"等标题文字）
    xAxis->setLabelColor(color);
    yAxis->setLabelColor(color);

    // 同步固定标签颜色
    applyYLabelTheme();

    replot();
}

// ============================================================================
// 固定 Y 轴标签（ptViewportRatio + 旋转 90°）
// ============================================================================

void SpecPlotBase::setupFixedYLabel(const QString &text)
{
    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

    // 隐藏内置 Y 轴标签
    yAxis->setLabel(QString());

    // 创建固定位置标签（竖向，从下往上读，锚在左边距区域）
    // AlignRight|AlignTop + rotation(-90): 文字右上角为锚点，
    if (!m_yLabelItem) {
        m_yLabelItem = new QCPItemText(this);
        m_yLabelItem->position->setType(QCPItemPosition::ptViewportRatio);
        m_yLabelItem->position->setCoords(DPR_REAL(0.005 * scale, dpr), DPR_REAL(0.015 * scale, dpr));  // 贴近左边，竖向文字
        m_yLabelItem->setRotation(-90);                     // 竖向（从下往上读）
        m_yLabelItem->setSelectable(false);
        m_yLabelItem->setClipToAxisRect(false);
        m_yLabelItem->setPositionAlignment(Qt::AlignRight | Qt::AlignTop);
    }
    m_yLabelItem->setText(text);
    applyYLabelTheme();
}

void SpecPlotBase::applyYLabelTheme()
{
    if (!m_yLabelItem) return;

    m_yLabelItem->setColor(yAxis->labelColor());
    m_yLabelItem->setFont(yAxis->labelFont());
}

// ============================================================================
// 鼠标事件
// ============================================================================

void SpecPlotBase::mousePressEvent(QMouseEvent *event)
{
    // ---- 先恢复上次可能未正确清理的状态 ----
    // 场景：右键菜单吞掉 mouseReleaseEvent → m_dragWasDisabled 跟踪变量被重置
    // 但实际 iRangeDrag 仍为 false。此处确保每次 press 前状态干净。
    if (m_dragWasDisabled) {
        setInteraction(QCP::iRangeDrag, true);
        m_dragWasDisabled = false;
    }
    if (m_dragAxesModified) {
        axisRect(0)->setRangeDragAxes(xAxis, yAxis);
        m_dragAxesModified = false;
    }

    m_dragPressZone     = getAxisZone(event->localPos());
    m_dragWasDisabled   = false;
    m_dragAxesModified  = false;

    QCPAxisRect *ar = axisRect(0);

    switch (m_dragPressZone) {
    case AxisZone::AtCenter:
    case AxisZone::AtOutside:
        // 数据绘图区 / 区域外：完全禁止拖拽
        if (interactions().testFlag(QCP::iRangeDrag)) {
            setInteraction(QCP::iRangeDrag, false);
            m_dragWasDisabled = true;
        }
        break;

    case AxisZone::AtXTop:
        // X 轴顶部区域：仅当 xAxis2 可见（有刻度）时才允许 X 方向拖拽
        if (ar && xAxis2->visible()) {
            ar->setRangeDragAxes(xAxis, nullptr);
            m_dragAxesModified = true;
        } else {
            // xAxis2 不可见时，该区域等价于 AtOutside，完全禁止拖拽
            if (interactions().testFlag(QCP::iRangeDrag)) {
                setInteraction(QCP::iRangeDrag, false);
                m_dragWasDisabled = true;
            }
        }
        break;

    case AxisZone::AtXBottom:
        // X 轴底部区域：始终允许 X 方向拖拽（xAxis 为主操作轴）
        if (ar) {
            ar->setRangeDragAxes(xAxis, nullptr);
            m_dragAxesModified = true;
        }
        break;

    case AxisZone::AtYLeft:
        // Y 轴左侧区域：始终允许 Y 方向拖拽（yAxis 为主操作轴）
        if (ar) {
            ar->setRangeDragAxes(nullptr, yAxis);
            m_dragAxesModified = true;
        }
        break;

    case AxisZone::AtYRight:
        // Y 轴右侧区域：仅当 yAxis2 可见（有刻度）时才允许 Y 方向拖拽
        if (ar && yAxis2->visible()) {
            ar->setRangeDragAxes(nullptr, yAxis);
            m_dragAxesModified = true;
        } else {
            // yAxis2 不可见时，该区域等价于 AtOutside，完全禁止拖拽
            if (interactions().testFlag(QCP::iRangeDrag)) {
                setInteraction(QCP::iRangeDrag, false);
                m_dragWasDisabled = true;
            }
        }
        break;
    }

    // 在轴区域按下时变为抓取手势（仅当对应轴可见时）
    const bool atDragZone =
        (m_dragPressZone == AxisZone::AtXTop    && xAxis2->visible()) ||
        (m_dragPressZone == AxisZone::AtXBottom) ||
        (m_dragPressZone == AxisZone::AtYLeft) ||
        (m_dragPressZone == AxisZone::AtYRight  && yAxis2->visible());
    if (atDragZone) {
        setCursor(Qt::ClosedHandCursor);
    }

    QCustomPlot::mousePressEvent(event);
}

void SpecPlotBase::mouseReleaseEvent(QMouseEvent *event)
{
    // 恢复拖拽交互
    if (m_dragWasDisabled) {
        setInteraction(QCP::iRangeDrag, true);
        m_dragWasDisabled = false;
    }

    // 恢复双向拖拽轴配置
    if (m_dragAxesModified) {
        axisRect(0)->setRangeDragAxes(xAxis, yAxis);
        m_dragAxesModified = false;
    }

    // 松开后根据当前所在区域恢复光标
    updateHoverCursor(getAxisZone(event->localPos()), event->pos());

    QCustomPlot::mouseReleaseEvent(event);
}

void SpecPlotBase::wheelEvent(QWheelEvent *event)
{
    QCPAxisRect *ar = axisRect(0);
    if (!ar) {
        QCustomPlot::wheelEvent(event);
        return;
    }

    // 保存原始缩放轴配置
    const auto origZoomH = ar->rangeZoomAxes(Qt::Horizontal);
    const auto origZoomV = ar->rangeZoomAxes(Qt::Vertical);

    // 根据鼠标所在轴区域决定缩放哪些轴
    const AxisZone zone = getAxisZone(event->posF());
    switch (zone) {
    case AxisZone::AtXTop:
    case AxisZone::AtXBottom:
        ar->setRangeZoomAxes(xAxis, nullptr);
        break;
    case AxisZone::AtYLeft:
    case AxisZone::AtYRight:
        ar->setRangeZoomAxes(nullptr, yAxis);
        break;
    default:
        // AtCenter / AtOutside / 其他：两轴同时缩放
        // ar->setRangeZoomAxes(xAxis, yAxis);
        ar->setRangeZoomAxes(xAxis, nullptr);
        break;
    }

    QCustomPlot::wheelEvent(event);

    // 恢复原始缩放轴配置
    ar->setRangeZoomAxes(origZoomH, origZoomV);
}

void SpecPlotBase::mouseMoveEvent(QMouseEvent *event)
{
    // 先调用基类实现，保留 QCustomPlot 的缩放等交互行为（拖拽已由 press 控制）
    QCustomPlot::mouseMoveEvent(event);

    const QPointF pos = event->localPos();
    const AxisZone zone = getAxisZone(pos);

    // 仅在悬停（非拖拽）时更新光标，拖拽中由 pressEvent 的 ClosedHandCursor 保持
    if (!(event->buttons() & Qt::LeftButton)) {
        updateHoverCursor(zone, event->pos());
    }

    if (zone != m_lastEmittedZone) {
        m_lastEmittedZone = zone;
        emit axisZoneChanged(zone);
    }

    // --- 检测 Quadrant ---
    const Quadrant quadrant = getQuadrant(pos);
    if (quadrant != m_lastEmittedQuadrant) {
        m_lastEmittedQuadrant = quadrant;
        emit quadrantChanged(quadrant);
    }

    // --- 检测 DataCoord ---
    const QPointF coord = getDataCoord(pos);
    if (coord != m_lastEmittedCoord) {
        m_lastEmittedCoord = coord;
        emit dataCoordChanged(coord);
    }
}

void SpecPlotBase::updateHoverCursor(AxisZone zone, const QPoint &/*mousePos*/)
{
    const bool atDragZone =
        (zone == AxisZone::AtXTop    && xAxis2->visible()) ||
        (zone == AxisZone::AtXBottom) ||
        (zone == AxisZone::AtYLeft)  ||
        (zone == AxisZone::AtYRight  && yAxis2->visible());
    if (atDragZone) {
        setCursor(Qt::OpenHandCursor);
    } else if (zone == AxisZone::AtCenter) {
        setCursor(Qt::CrossCursor);
    } else {
        setCursor(Qt::ArrowCursor);
    }
}

// ============================================================================
// 私有槽 — 范围限制即时拦截
// ============================================================================

void SpecPlotBase::onXRangeChanged(const QCPRange &newRange)
{
    m_xTickLabels->setMaxTick(newRange.upper - newRange.lower);
    // 递归防护：clamp 后的 setRange 会再次触发本槽，通过标志位短路
    if (m_enforcingXLimits || !m_hasXLimit) {
        return;
    }

    const double lower = newRange.lower;
    const double upper = newRange.upper;
    const bool needClamp = (lower < m_xLimitMin || upper > m_xLimitMax);
    if (!needClamp) {
        invalidateCache();
        return;
    }

    const double rangeSize   = upper - lower;                // 保持范围大小不变
    const double limitSize   = m_xLimitMax - m_xLimitMin;

    double clampedLower = lower;
    double clampedUpper = upper;

    if (rangeSize > limitSize) {
        // 范围本身超过限制范围，直接贴合限制边界
        clampedLower = m_xLimitMin;
        clampedUpper = m_xLimitMax;
    } else {
        // 保持范围大小不变，只平移：一侧碰壁就平移另一侧
        if (clampedLower < m_xLimitMin) {
            clampedLower = m_xLimitMin;
            clampedUpper = clampedLower + rangeSize;
        }
        if (clampedUpper > m_xLimitMax) {
            clampedUpper = m_xLimitMax;
            clampedLower = clampedUpper - rangeSize;
        }
    }

    m_enforcingXLimits = true;
    xAxis->setRange(QCPRange(clampedLower, clampedUpper));
    m_enforcingXLimits = false;

    invalidateCache();
}

void SpecPlotBase::onYRangeChanged(const QCPRange &newRange)
{
    // 递归防护
    if (m_enforcingYLimits || !m_hasYLimit) {
        return;
    }

    const double lower = newRange.lower;
    const double upper = newRange.upper;
    const bool needClamp = (lower < m_yLimitMin || upper > m_yLimitMax);
    if (!needClamp) {
        invalidateCache();
        return;
    }

    const double rangeSize   = upper - lower;
    const double limitSize   = m_yLimitMax - m_yLimitMin;

    double clampedLower = lower;
    double clampedUpper = upper;

    if (rangeSize > limitSize) {
        clampedLower = m_yLimitMin;
        clampedUpper = m_yLimitMax;
    } else {
        if (clampedLower < m_yLimitMin) {
            clampedLower = m_yLimitMin;
            clampedUpper = clampedLower + rangeSize;
        }
        if (clampedUpper > m_yLimitMax) {
            clampedUpper = m_yLimitMax;
            clampedLower = clampedUpper - rangeSize;
        }
    }

    m_enforcingYLimits = true;
    yAxis->setRange(QCPRange(clampedLower, clampedUpper));
    m_enforcingYLimits = false;

    invalidateCache();
}

void SpecPlotBase::enforceRangeLimits()
{
    // 供 setXRangeLimit / setYRangeLimit 等公开 API 调用
    // 直接读取当前范围并 clamp（信号拦截会处理 setRange 的触发）
    if (m_hasXLimit) {
        QCPRange xr = xAxis->range();
        if (xr.lower < m_xLimitMin || xr.upper > m_xLimitMax) {
            if (xr.lower < m_xLimitMin) xr.lower = m_xLimitMin;
            if (xr.upper > m_xLimitMax) xr.upper = m_xLimitMax;
            xAxis->setRange(xr);
        }
    }
    if (m_hasYLimit) {
        QCPRange yr = yAxis->range();
        if (yr.lower < m_yLimitMin || yr.upper > m_yLimitMax) {
            if (yr.lower < m_yLimitMin) yr.lower = m_yLimitMin;
            if (yr.upper > m_yLimitMax) yr.upper = m_yLimitMax;
            yAxis->setRange(yr);
        }
    }
}

// ============================================================================
// 缓存管理
// ============================================================================

void SpecPlotBase::invalidateCache() const
{
    m_cache.valid = false;
}
