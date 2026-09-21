#include "HQTFMark.h"
#include "../plot/qcustomplot.h"
#include "../plot/SpecPlotBase.h"   // Quadrant 枚举完整定义（drawTooltip 中 switch 需要）
#include "comm/CommonMacros.h"
#include <QPainter>
#include <QFontMetrics>

#include "comm/ScreenScale.h"

HQTFMark::HQTFMark(int64_t id, double freqStart, double freqStop, HQTFMarkStyle style,
               const QString &cfText, const QString &bwText)
    : m_id(id)
    , m_freqStart(freqStart)
    , m_freqStop(freqStop)
    , m_style(style)
    , m_cfText(cfText)
    , m_bwText(bwText)
{
}

QColor HQTFMark::styleColor() const
{
    switch (m_style) {
    case HQTFMarkStyle::Normal:
        return QColor(6, 228, 233);
    case HQTFMarkStyle::Error:
        return QColor(230, 62, 62);
    case HQTFMarkStyle::Warning:
        return QColor(255, 186, 0);
    case HQTFMarkStyle::OffLine:
        return QColor(128, 128, 128);
    }
    return QColor(6, 228, 233);
}

// ============================================================================
// containsPoint — 命中检测
// ============================================================================

bool HQTFMark::containsPoint(const QPoint &pos, const QCPAxisRect *axisRect,
                           const QCPAxis *xAxis,
                           double yPixelTop, double yPixelBottom) const
{
    if (!axisRect || !xAxis) return false;

    const QRect ar = axisRect->rect();
    if (!ar.contains(pos)) return false;

    // ---- X 轴范围检测（频率） ----
    const double leftPx  = xAxis->coordToPixel(m_freqStart);
    const double rightPx = xAxis->coordToPixel(m_freqStop);
    const int boxLeft    = static_cast<int>(qMin(leftPx, rightPx));
    const int boxWidthPx = qMax(4, static_cast<int>(qAbs(rightPx - leftPx)));

    // ---- Y 轴范围检测 ----
    int boxTop, boxHeight;
    if (yPixelTop >= 0.0 && yPixelBottom >= 0.0) {
        boxTop    = static_cast<int>(qMin(yPixelTop, yPixelBottom));
        boxHeight = qMax(4, static_cast<int>(qAbs(yPixelBottom - yPixelTop)));
    } else {
        boxTop    = ar.top();
        boxHeight = qMax(4, static_cast<int>(qMax(yPixelTop, yPixelBottom)) - boxTop);
    }

    return QRect(boxLeft, boxTop, boxWidthPx, boxHeight).contains(pos);
}

// ============================================================================
// draw — 绘制标记框
// ============================================================================

void HQTFMark::draw(QPainter *painter, const QCPAxisRect *axisRect,
                  const QCPAxis *xAxis, const QCPAxis *yAxis,
                  double yPixelTop, double yPixelBottom) const
{
    Q_UNUSED(yAxis);
    if (!painter || !axisRect || !xAxis) return;

    const QRect ar      = axisRect->rect();
    const QColor color  = styleColor();

    // ---- 计算框体左右边界（频率 → 像素） ----
    const double leftPx  = xAxis->coordToPixel(m_freqStart);
    const double rightPx = xAxis->coordToPixel(m_freqStop);
    const int boxLeft    = static_cast<int>(qMin(leftPx, rightPx));
    const int boxWidthPx = qMax(4, static_cast<int>(qAbs(rightPx - leftPx)));

    // 水平方向不在可见区域则跳过
    if (rightPx < ar.left() || leftPx > ar.right()) return;

    // ---- 计算框体 Y 轴范围 ----
    const int boxBottom = static_cast<int>(qMax(yPixelTop, yPixelBottom));
    int boxTop    = static_cast<int>(qMin(yPixelTop, yPixelBottom));
    int boxHeight = qMax(4, boxBottom - boxTop);

    // 垂直方向裁剪：如果框体完全在 axisRect 之外则跳过
    if (boxTop + boxHeight < ar.top() || boxTop > ar.bottom()) return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setClipRect(ar);

    // ---- 绘制有色矩形框体 ----
    QColor fillColor = color;
    if (m_selected) {
        fillColor.setAlpha(70);
        painter->setBrush(fillColor);
        painter->setPen(QPen(color.lighter(130), 2, Qt::SolidLine));
    } else {
        fillColor.setAlpha(24);
        painter->setBrush(fillColor);
        painter->setPen(QPen(color, 1, Qt::DashLine));
    }
    painter->drawRoundedRect(QRect(boxLeft, boxTop, boxWidthPx, boxHeight), 1, 1);

    painter->restore();
}

// ============================================================================
// drawTooltip — 在鼠标位置附近绘制 CF/BW 悬浮提示
// ============================================================================

void HQTFMark::drawTooltip(QPainter *painter, const QPointF &mousePos, Quadrant quadrant) const
{
    if (!painter) return;
    double ss = ScreenScale::instance().scale();
    qreal dpr = ScreenScale::instance().dpr();

    const QColor color = styleColor();
    const QString cfText = QStringLiteral("中心频率: %1").arg(m_cfText);
    // "带宽"仅 2 个汉字，中间插入 2 个全角空格（U+3000，与汉字等宽）撑满 4 字宽，
    // 使冒号与上一行"中心频率"对齐
    const QString bwText = QStringLiteral("带　　宽: %1").arg(m_bwText);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    QFont font = painter->font();
    font.setPointSize(DPR_INT(12 * ss, dpr, 0));
    painter->setFont(font);

    QFontMetrics fm(font);
    const int textW = fm.horizontalAdvance(cfText) + DPR_INT(12 * ss, dpr, 0);   // 左右各留 6px 内边距
    const int textH = fm.height() * 2 + DPR_INT(6 * ss, dpr, 0);                 // 上下各留 3px 内边距
    const int gap   = DPR_INT(16 * ss, dpr, 0);                                  // 文字与鼠标的间距

    // 根据象限确定文字绘制位置（使文字远离视图中心，避免被边缘裁切）
    int textX, textY;
    switch (quadrant) {
    case Quadrant::Q1:   // 右上 → 文字在鼠标左下方
        textX = static_cast<int>(mousePos.x() - textW - gap);
        textY = static_cast<int>(mousePos.y() + gap);
        break;
    case Quadrant::Q2:   // 左上 → 文字在鼠标右下方
        textX = static_cast<int>(mousePos.x() + gap);
        textY = static_cast<int>(mousePos.y() + gap);
        break;
    case Quadrant::Q3:   // 左下 → 文字在鼠标右上方
        textX = static_cast<int>(mousePos.x() + gap);
        textY = static_cast<int>(mousePos.y() - textH - gap);
        break;
    case Quadrant::Q4:   // 右下 → 文字在鼠标左上方
        textX = static_cast<int>(mousePos.x() - textW - gap);
        textY = static_cast<int>(mousePos.y() - textH - gap);
        break;
    default:             // 原点/轴上/范围外 → 默认右下方
        textX = static_cast<int>(mousePos.x() + gap);
        textY = static_cast<int>(mousePos.y() + gap);
        break;
    }

    const QRect textRect(textX, textY, textW, textH);
    const QRect cfTextRect(textX, textY, textW, textH / 2);
    const QRect bwTextRect(textX, textY + textH / 2, textW, textH / 2);

    // 半透明深色背景（悬浮在谱线上方时保证文字可读）
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(10, 20, 40, 200));
    painter->drawRoundedRect(textRect, 4, 4);

    // 文字颜色与标记框颜色一致（Normal=青色, Warning=橙色, Error=红色）
    painter->setPen(color);
    painter->drawText(cfTextRect, Qt::AlignLeft, cfText);
    painter->drawText(bwTextRect, Qt::AlignLeft, bwText);

    painter->restore();
}
