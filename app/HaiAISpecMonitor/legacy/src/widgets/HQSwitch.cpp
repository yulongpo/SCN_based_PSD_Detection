#include "HQSwitch.h"
#include "comm/CommonMacros.h"
#include "comm/ScreenScale.h"
#include "comm/FontManager.h"
#include <QPainter>
#include <QMouseEvent>
#include <QFontMetrics>

HQSwitch::HQSwitch(const QString &text, QWidget *parent)
    : QWidget(parent), m_text(text)
{
    setCursor(Qt::PointingHandCursor);
}

void HQSwitch::setOn(bool on)
{
    if (m_on != on) {
        m_on = on;
        update();
        emit toggled(m_on);
    }
}

void HQSwitch::setText(const QString &text)
{
    if (m_text != text) {
        m_text = text;
        update();
    }
}

void HQSwitch::setOnColor(const QColor &color)
{
    m_onColor = color;
    update();
}

void HQSwitch::setOffColor(const QColor &color)
{
    m_offColor = color;
    update();
}

QSize HQSwitch::sizeHint() const
{
    const auto &ss = ScreenScale::instance();
    const qreal dpr   = ss.dpr();
    const double scale = ss.scale();

    const int switchW = DPR_INT(40 * scale, dpr, 28);
    const int switchH = DPR_INT(22 * scale, dpr, 16);

    int textW = 0;
    if (!m_text.isEmpty()) {
        const int labelFs = DPR_INT(13 * scale, dpr, 9);
        QFont font = FontManager::instance().font(labelFs, QFont::Normal);
        QFontMetrics fm(font);
        textW = fm.horizontalAdvance(m_text);
    }
    const int textSpacing = m_text.isEmpty() ? 0 : DPR_INT(6 * scale, dpr, 4);

    return QSize(textW + textSpacing + switchW, switchH + DPR_INT(4 * scale, dpr, 2));
}

void HQSwitch::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const auto &ss = ScreenScale::instance();
    const qreal dpr   = ss.dpr();
    const double scale = ss.scale();

    const int switchW  = DPR_INT(40 * scale, dpr, 28);
    const int switchH  = DPR_INT(22 * scale, dpr, 16);
    const int knobDiam = switchH - 4;
    const int switchY  = (height() - switchH) / 2;

    // 左侧文字
    int textW = 0;
    int switchX = 0;
    if (!m_text.isEmpty()) {
        const int textSpacing = DPR_INT(6 * scale, dpr, 4);
        const int labelFs     = DPR_INT(13 * scale, dpr, 9);
        QFont font = FontManager::instance().font(labelFs, QFont::Normal);
        p.setFont(font);
        p.setPen(QColor(255, 255, 255));
        QFontMetrics fm(font);
        textW = fm.horizontalAdvance(m_text);
        QRectF textRect(0, 0, textW, height());
        p.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, m_text);
        switchX = textW + textSpacing;
    }

    // 开关背景胶囊
    QRectF bgRect(switchX, switchY, switchW, switchH);
    p.setPen(Qt::NoPen);
    p.setBrush(m_on ? m_onColor : m_offColor);
    p.drawRoundedRect(bgRect, switchH / 2, switchH / 2);

    // 圆形滑块
    const int knobXPos = switchX + (m_on ? switchW - knobDiam - 2 : 2);
    const int knobYPos = switchY + 2;
    p.setBrush(Qt::white);
    p.drawEllipse(QPointF(knobXPos + knobDiam / 2.0, knobYPos + knobDiam / 2.0),
                  knobDiam / 2.0, knobDiam / 2.0);
}

void HQSwitch::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && rect().contains(event->pos())) {
        m_on = !m_on;
        update();
        emit toggled(m_on);
    }
    QWidget::mouseReleaseEvent(event);
}
