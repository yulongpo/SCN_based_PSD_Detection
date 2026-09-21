#include "HQToolButton.h"
#include "comm/CommonMacros.h"
#include "comm/ScreenScale.h"
#include <QPainter>
#include <QPainterPath>

HQToolButton::HQToolButton(QWidget *parent)
    : QPushButton(parent)
{
    setFlat(true);
    setCursor(Qt::PointingHandCursor);
}

void HQToolButton::setHQIcon(const QIcon &icon, int iconW, int iconH)
{
    m_icon = icon;
    m_iconSize = QSize(iconW, iconH);
    update();
}

void HQToolButton::setHQRadius(int logicalRadius)
{
    m_logicalRadius = logicalRadius;
    update();
}

void HQToolButton::setBorderColor(const QColor &color)
{
    m_borderColor = color;
    update();
}

void HQToolButton::setHoverBackground(const QColor &color)
{
    m_hoverBg = color;
    update();
}

void HQToolButton::setTextColor(const QColor &color)
{
    m_textColor = color;
    update();
}

void HQToolButton::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

    const int w = width();
    const int h = height();

    // ---- 几何参数 ----
    // 边框宽度：1 逻辑像素 → 设备像素对齐
    const int borderW  = qMax(1, DPR_INT(1 * scale, dpr, 1));
    const qreal halfBW = borderW * 0.5;
    // 圆角半径（设备像素）
    const int radius = qMax(1, DPR_INT(m_logicalRadius * scale, dpr, 2));
    // 圆角传参：QPainterPath 传入半径需要减去 halfBW 以防 corner 溢出
    const qreal cornerR = qMax(0.0, radius - halfBW);

    // 内容矩形（缩进 halfBW 使边框居中于边缘）
    const QRectF contentRect(halfBW, halfBW, w - borderW, h - borderW);

    QPainterPath clipPath;
    clipPath.addRoundedRect(contentRect, cornerR, cornerR);

    // ---- 1. 悬停背景 ----
    if (m_hovered)
        p.fillPath(clipPath, m_hoverBg);

    // ---- 2. 图标 + 文字（整体居中） ----
    const int spacing = DPR_INT(4 * scale, dpr, 2);

    // 文本宽度
    QFont font = p.font();
    QFontMetrics fm(font);
    const QString btnText = text();
    const int textW = fm.horizontalAdvance(btnText);

    const bool hasIcon = !m_icon.isNull();
    const int iconAreaW = hasIcon ? (m_iconSize.width() + spacing) : 0;
    const int totalW = iconAreaW + textW;

    int contentX = (w - totalW) / 2;
    if (contentX < spacing)
        contentX = spacing;

    // 图标
    if (hasIcon) {
        const int iconX = contentX;
        const int iconY = (h - m_iconSize.height()) / 2;
        const QRect iconRect(iconX, iconY, m_iconSize.width(), m_iconSize.height());
        m_icon.paint(&p, iconRect, Qt::AlignCenter,
                     isEnabled() ? QIcon::Normal : QIcon::Disabled);
        contentX += iconAreaW;
    }

    // 文字
    {
        QColor tc = m_textColor.isValid() ? m_textColor : palette().buttonText().color();
        if (!isEnabled())
            tc = QColor(85, 85, 85);
        p.setPen(tc);
        p.setFont(font);
        p.drawText(QRectF(contentX, 0, textW, h), Qt::AlignLeft | Qt::AlignVCenter, btnText);
    }

    // ---- 3. 边框 ----
    {
        QPainterPath borderPath;
        borderPath.addRoundedRect(
            QRectF(halfBW, halfBW, w - borderW, h - borderW),
            cornerR, cornerR);
        p.setPen(QPen(m_borderColor, borderW));
        p.setBrush(Qt::NoBrush);
        p.drawPath(borderPath);
    }
}

void HQToolButton::enterEvent(QEvent *event)
{
    m_hovered = true;
    update();
    QPushButton::enterEvent(event);
}

void HQToolButton::leaveEvent(QEvent *event)
{
    m_hovered = false;
    update();
    QPushButton::leaveEvent(event);
}
