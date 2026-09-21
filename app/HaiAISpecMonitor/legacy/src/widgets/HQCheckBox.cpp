#include "HQCheckBox.h"
#include "comm/CommonMacros.h"
#include "comm/ScreenScale.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QFontMetrics>

// ============================================================================
// HQCheckBox
// ============================================================================

HQCheckBox::HQCheckBox(QWidget *parent)
    : QWidget(parent)
{
    setCursor(Qt::PointingHandCursor);
}

void HQCheckBox::setChecked(bool checked)
{
    if (m_checked != checked) {
        m_checked = checked;
        update();
        emit stateChanged(m_checked ? Qt::Checked : Qt::Unchecked);
    }
}

void HQCheckBox::setText(const QString &text)
{
    m_text = text;
    update();
}

void HQCheckBox::setTextColor(const QColor &color)
{
    m_textColor = color;
    update();
}

void HQCheckBox::setMinBoxSize(int minSize)
{
    m_minBoxSize = minSize;
    updateGeometry();
    update();
}

QSize HQCheckBox::sizeHint() const
{
    const auto &ss = ScreenScale::instance();
    const int boxPx = DPR_INT(m_boxSize * ss.scale(), ss.dpr(), m_minBoxSize);

    int textW = 0;
    if (!m_text.isEmpty()) {
        QFontMetrics fm(font());
        textW = fm.horizontalAdvance(m_text);
    }
    const int spacingPx = m_text.isEmpty() ? 0 : DPR_INT(m_spacing * ss.scale(), ss.dpr(), m_spacing);
    return QSize(boxPx + spacingPx + textW, boxPx);
}

void HQCheckBox::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    const auto &ss   = ScreenScale::instance();
    const qreal dpr  = ss.dpr();
    const double sc  = ss.scale();

    const int boxPx = DPR_INT(m_boxSize * sc, dpr, m_minBoxSize);

    // 复选框：垂直居中靠左
    const int boxY = (height() - boxPx) / 2;
    const QRectF boxRect(0, boxY, boxPx, boxPx);
    draw(&p, boxRect, m_checked);

    // 文字：复选框右侧
    if (!m_text.isEmpty()) {
        const int spacingPx = DPR_INT(m_spacing * sc, dpr, m_spacing);
        const int textX = boxPx + spacingPx;

        p.setPen(m_textColor);
        p.setFont(font());
        p.drawText(QRectF(textX, 0, width() - textX, height()),
                   Qt::AlignLeft | Qt::AlignVCenter, m_text);
    }
}

void HQCheckBox::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && rect().contains(event->pos()))
        setChecked(!m_checked);
    QWidget::mouseReleaseEvent(event);
}

// ---- static helpers ----

void HQCheckBox::draw(QPainter *painter, const QRectF &rect, bool checked)
{
    const qreal side = qMin(rect.width(), rect.height());
    if (side <= 2) return;

    const qreal x = rect.center().x() - side / 2;
    const qreal y = rect.center().y() - side / 2;
    const QRectF box(x, y, side, side);
    const qreal radius = 4.0;

    QPen borderPen(QColor(115, 115, 120), 1.0);
    painter->setPen(borderPen);

    if (checked) {
        painter->setBrush(QColor(10, 140, 254));
        painter->drawRoundedRect(box, radius, radius);

        QPen checkPen(Qt::white, qMax(2.0, side / 5));
        checkPen.setCapStyle(Qt::FlatCap);
        checkPen.setJoinStyle(Qt::MiterJoin);
        painter->setPen(checkPen);

        QPainterPath path;
        const qreal l = box.left() + side * 0.22;
        const qreal r = box.right() - side * 0.22;
        const qreal t = box.top() + side * 0.28;
        const qreal b = box.bottom() - side * 0.22;
        path.moveTo(l, box.center().y());
        path.lineTo(box.center().x(), b);
        path.lineTo(r, t);
        painter->drawPath(path);
    } else {
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(box, radius, radius);
    }
}

QPixmap HQCheckBox::renderPixmap(bool checked, int logicalSize)
{
    const auto &ss  = ScreenScale::instance();
    const qreal dpr = ss.dpr();
    const int cSz   = DPR_INT(logicalSize * ss.scale(), dpr, logicalSize);
    const int cPh   = qRound(cSz * dpr);

    QPixmap pix(cPh, cPh);
    pix.fill(Qt::transparent);
    pix.setDevicePixelRatio(dpr);

    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing, true);
    draw(&p, QRectF(0.5, 0.5, cSz - 1, cSz - 1), checked);
    p.end();
    return pix;
}

