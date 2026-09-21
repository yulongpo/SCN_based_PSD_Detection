#include "HQButton.h"

#include "comm/CommonMacros.h"
#include "comm/FontManager.h"
#include "comm/ScreenScale.h"
#include "comm/ThemeManager.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>

HQButton::HQButton(QWidget *parent)
    : QPushButton(parent)
    , m_style(Primary)
    , m_iconType(NoIcon)
    , m_emphasisAnimation(new QPropertyAnimation(this, "emphasisProgress", this))
    , m_emphasisProgress(0.0)
    , m_hovered(false)
    , m_pressed(false)
{
    setAttribute(Qt::WA_Hover, true);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    setFlat(true);
    setStyleSheet(QStringLiteral("QPushButton { border: none; }"));

    m_emphasisAnimation->setDuration(200);
    m_emphasisAnimation->setEasingCurve(QEasingCurve::OutCubic);

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &HQButton::applyThemeAppearance);
}

void HQButton::setButtonStyle(Style style)
{
    m_style = style;
    update();
}

void HQButton::setIconType(Icon icon)
{
    m_iconType = icon;
    update();
}

// --------------- 事件 ---------------

void HQButton::enterEvent(QEvent *event)
{
    // disabled 状态下不响应 hover 效果
    if (isEnabled()) {
        m_hovered = true;
        animateEmphasisTo(targetEmphasis());
    }
    QPushButton::enterEvent(event);
}

void HQButton::leaveEvent(QEvent *event)
{
    // disabled 状态下清除所有交互状态
    if (isEnabled()) {
        m_hovered = false;
        m_pressed = false;
        animateEmphasisTo(targetEmphasis());
    }
    QPushButton::leaveEvent(event);
}

void HQButton::mousePressEvent(QMouseEvent *event)
{
    // disabled 状态下不响应按下效果
    if (isEnabled() && event->button() == Qt::LeftButton) {
        m_pressed = true;
        animateEmphasisTo(targetEmphasis());
    }
    QPushButton::mousePressEvent(event);
}

void HQButton::mouseReleaseEvent(QMouseEvent *event)
{
    // disabled 状态下不响应释放效果
    if (isEnabled() && event->button() == Qt::LeftButton) {
        m_pressed = false;
        animateEmphasisTo(targetEmphasis());
    }
    QPushButton::mouseReleaseEvent(event);
}

void HQButton::changeEvent(QEvent *event)
{
    // 当启用/禁用状态变化时，更新鼠标光标样式并重绘
    if (event->type() == QEvent::EnabledChange) {
        setCursor(isEnabled() ? Qt::PointingHandCursor : Qt::ArrowCursor);
        // 禁用时重置交互状态，避免残留 hover/press 效果
        if (!isEnabled()) {
            m_hovered = false;
            m_pressed = false;
            m_emphasisAnimation->stop();
            m_emphasisProgress = 0.0;
        }
        update();
    }
    QPushButton::changeEvent(event);
}

// --------------- 绘制 ---------------

void HQButton::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const auto &ss = ScreenScale::instance();
    const int radius = DPR_INT(8 * ss.scale(), ss.dpr(), 0);

    const QString prefix = (m_style == Primary || m_style == Primary_normal)
        ? QStringLiteral("hqButton.primary.")
        : QStringLiteral("hqButton.error.");

    const QColor baseBg   = themeColor(prefix + "backgroundColor", m_style == Primary ? QColor("#0A8CFE") : (m_style == Primary_normal ? QColor(7, 33, 64) : QColor(230, 62, 62, 48)));
    const QColor hoverBg  = themeColor(prefix + "hoverBackgroundColor", m_style == Primary ? QColor("#2C9EFF") : (m_style == Primary_normal ? QColor(7, 33, 64) : QColor(230, 62, 62, 72)));
    const QColor pressBg  = themeColor(prefix + "pressedBackgroundColor", m_style == Primary ? QColor("#097ADF") : (m_style == Primary_normal ? QColor(7, 33, 64) : QColor(230, 62, 62, 96)));
    // 边框/图标颜色
    const QColor accentColor = themeColor(prefix + "color", m_style == Error ? QColor("#E63E3E") : QColor("#0A8CFE"));

    // disabled 状态下降低整体不透明度，使按钮呈现灰化效果
    const qreal disabledOpacity = isEnabled() ? 1.0 : 0.4;

    // 背景色：press > hover > normal
    QColor bg = baseBg;
    if (m_pressed) {
        bg = pressBg;
    } else if (m_emphasisProgress > 0.0) {
        bg = QColor::fromRgbF(
            baseBg.redF()   + (hoverBg.redF()   - baseBg.redF())   * m_emphasisProgress,
            baseBg.greenF() + (hoverBg.greenF() - baseBg.greenF()) * m_emphasisProgress,
            baseBg.blueF()  + (hoverBg.blueF()  - baseBg.blueF())  * m_emphasisProgress,
            baseBg.alphaF() + (hoverBg.alphaF() - baseBg.alphaF()) * m_emphasisProgress);
    }

    // disabled 状态下应用全局透明度
    painter.setOpacity(disabledOpacity);

    // 背景
    const QRectF box(0.5, 0.5, width() - 1.0, height() - 1.0);
    QPainterPath path;
    path.addRoundedRect(box, radius, radius);

    // 透明背景 + 边框
    painter.setPen(QPen(accentColor, 2.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(box, radius, radius);
    // hover/press 高亮叠加（disabled 时不叠加高亮，因为 disabledOpacity 已处理视觉效果）
    if (isEnabled()) {
        if (m_pressed) {
            painter.fillPath(path, QColor(accentColor.red(), accentColor.green(), accentColor.blue(), 30));
        } else if (m_emphasisProgress > 0.0) {
            painter.fillPath(path, QColor(accentColor.red(), accentColor.green(), accentColor.blue(), 18));
        }
    }

    // 文字字体
    painter.setFont(font());

    const bool hasIcon = (m_iconType != NoIcon);
    if (hasIcon) {
        const int padding = DPR_INT(8 * ss.scale(), ss.dpr(), 4);
        const int maxContentW = width() - padding * 2;

        QFontMetrics fm(font());
        const int textW = fm.horizontalAdvance(text());

        // 图标尺寸自适应：icon 和文字的总宽不能超过可用空间
        const int iconPad = DPR_INT(4 * ss.scale(), ss.dpr(), 2);
        int iconSz = DPR_INT(16 * ss.scale(), ss.dpr(), 9);
        if (iconSz + iconPad + textW > maxContentW) {
            iconSz = qMax(DPR_INT(10 * ss.scale(), ss.dpr(), 6), maxContentW - iconPad - textW);
        }

        const int totalW = iconSz + iconPad + textW;
        const int startX = (width() - totalW) / 2;
        const int iconY = (height() - iconSz) / 2;
        const QRectF iconRect(startX, iconY, iconSz, iconSz);

        painter.setPen(Qt::NoPen);
        painter.setBrush(accentColor);
        switch (m_iconType) {
        case Start: drawStartIcon(painter, iconRect); break;
        case Pause: drawPauseIcon(painter, iconRect); break;
        case Stop:  drawStopIcon(painter, iconRect);  break;
        default: break;
        }

        painter.setPen(Qt::white);
        painter.drawText(QRectF(startX + iconSz + iconPad, 0, textW, height()),
                         Qt::AlignLeft | Qt::AlignVCenter, text());
    } else {
        painter.setPen(Qt::white);
        painter.drawText(box, Qt::AlignCenter, text());
    }

    // 恢复透明度，避免影响后续绘制
    painter.setOpacity(1.0);
}

// --------------- 图标绘制 ---------------

void HQButton::drawStartIcon(QPainter &painter, const QRectF &r)
{
    // 外围圆环
    const qreal circleDia = r.height();
    const QRectF circleRect(r.center().x() - circleDia / 2.0,
                            r.center().y() - circleDia / 2.0,
                            circleDia, circleDia);
    painter.save();
    painter.setPen(QPen(Qt::white, 1.5));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(circleRect);
    painter.restore();

    // 右指三角形 ▶
    const qreal h = r.height() * 0.75;
    const qreal w = h * 0.75;                        // 宽高比约 0.75，视觉均衡
    const qreal l = r.center().x() - w / 3.0;          // 三角形质心与圆同心（质心在 l + w/3）
    const qreal t = r.center().y() - h / 2.0;
    QPainterPath tri;
    tri.moveTo(l, t);
    tri.lineTo(l + w, r.center().y());
    tri.lineTo(l, t + h);
    tri.closeSubpath();
    painter.drawPath(tri);
}

void HQButton::drawPauseIcon(QPainter &painter, const QRectF &r)
{
    // 两条竖线 ⏸
    const qreal barW = r.width() * 0.22;
    const qreal gap = r.width() * 0.16;
    const qreal l = r.x() + (r.width() - barW * 2 - gap) / 2.0;
    const qreal t = r.y() + r.height() * 0.15;
    const qreal h = r.height() * 0.7;
    painter.drawRect(QRectF(l, t, barW, h));
    painter.drawRect(QRectF(l + barW + gap, t, barW, h));
}

void HQButton::drawStopIcon(QPainter &painter, const QRectF &r)
{
    // 正方形 ■
    const qreal s = r.width() * 0.5;
    const qreal l = r.x() + (r.width() - s) / 2.0;
    const qreal t = r.y() + (r.height() - s) / 2.0;
    painter.drawRect(QRectF(l, t, s, s));
}

// --------------- 辅助 ---------------

void HQButton::applyThemeAppearance(bool night)
{
    Q_UNUSED(night);
    update();
}

void HQButton::setEmphasisProgress(qreal progress)
{
    progress = qBound(0.0, progress, 1.0);
    if (qFuzzyCompare(m_emphasisProgress, progress)) {
        return;
    }
    m_emphasisProgress = progress;
    update();
}

QColor HQButton::themeColor(const QString &key, const QColor &fallback) const
{
    const QColor color = ThemeManager::instance().color(key);
    return color.isValid() ? color : fallback;
}

void HQButton::animateEmphasisTo(qreal value)
{
    value = qBound(0.0, value, 1.0);
    m_emphasisAnimation->stop();
    m_emphasisAnimation->setStartValue(m_emphasisProgress);
    m_emphasisAnimation->setEndValue(value);
    m_emphasisAnimation->start();
}

qreal HQButton::targetEmphasis() const
{
    if (m_pressed) return 0.0;
    return m_hovered ? 1.0 : 0.0;
}
