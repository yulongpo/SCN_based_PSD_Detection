#include "HQColorWidget.h"
#include <QPainter>
#include <QMouseEvent>
#include <QLinearGradient>
#include "comm/CommonMacros.h"
#include "comm/ScreenScale.h"

HQColorWidget::HQColorWidget(QWidget *parent)
    : QWidget(parent)
{
    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

    setFixedSize(DPR_INT(kWidgetWidth * scale, dpr, 0), DPR_INT(kWidgetHeight * scale, dpr, 0));
    setCursor(Qt::CrossCursor);

    int padding = DPR_INT(kPadding * scale, dpr, 0);
    int sliderHeight = DPR_INT(kSliderHeight * scale, dpr, 0);
    int sliderGap = DPR_INT(kSliderGap * scale, dpr, 0);
    // 色域选取区：顶部剩余空间
    const int pickerH = height() - padding * 2 - sliderHeight * 2 - sliderGap * 2;
    m_pickerRect = QRect(padding, padding, width() - padding * 2, pickerH);

    // 色相滑块（第一个拖动条）
    const int sliderY = m_pickerRect.bottom() + sliderGap;
    const int sliderW = width() - padding * 2;
    m_hueRect   = QRect(padding, sliderY, sliderW, sliderHeight);

    // 透明度滑块（第二个拖动条）
    m_alphaRect = QRect(padding, sliderY + sliderHeight + sliderGap,
                        sliderW, sliderHeight);

    updatePickerPixmap();
}

QColor HQColorWidget::currentColor() const
{
    return QColor::fromHsv(m_hue, m_sat, m_val, m_alpha);
}

void HQColorWidget::setCurrentColor(const QColor &color)
{
    m_hue   = qMax(0, color.hue());
    m_sat   = color.saturation();
    m_val   = color.value();
    m_alpha = color.alpha();

    updatePickerPixmap();
    update();
    emit colorChanged(currentColor());
}

// ─── 缓存色域像素图 ────────────────────────────────────────────────
void HQColorWidget::updatePickerPixmap()
{
    const QSize sz = m_pickerRect.size();
    if (sz.isEmpty()) return;

    m_pickerPixmap = QPixmap(sz);
    m_pickerPixmap.fill(Qt::transparent);

    QPainter p(&m_pickerPixmap);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    const QColor pure = QColor::fromHsv(m_hue, 255, 255);

    // 1) 纯色基底
    p.fillRect(m_pickerPixmap.rect(), pure);

    // 2) 从上到下的黑色渐变：降低明度，顶部亮 → 底部暗
    QLinearGradient vGrad(0, 0, 0, sz.height());
    vGrad.setColorAt(0, Qt::transparent);
    vGrad.setColorAt(1, Qt::black);
    p.fillRect(m_pickerPixmap.rect(), vGrad);

    // 3) 从左到右的白色渐变：降低饱和度，左侧淡 → 右侧深
    QLinearGradient hGrad(0, 0, sz.width(), 0);
    hGrad.setColorAt(0, Qt::white);
    hGrad.setColorAt(1, Qt::transparent);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    p.fillRect(m_pickerPixmap.rect(), hGrad);
}

// ─── 绘制 ──────────────────────────────────────────────────────────
void HQColorWidget::paintEvent(QPaintEvent *)
{
    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // 控件背景
    p.fillRect(rect(), QColor(0x2D, 0x2D, 0x2D));

    // ── 色域选取区 ──
    p.drawPixmap(m_pickerRect.topLeft(), m_pickerPixmap);
    p.setPen(QColor(0x55, 0x55, 0x55));
    p.drawRect(m_pickerRect.adjusted(0, 0, -1, -1));

    // 当前颜色指示器（双层圆环）
    const QPoint ip = indicatorPos(currentColor());
    if (m_pickerRect.contains(ip)) {
        int radius = DPR_INT(kKnobRadius * scale, dpr, 0);
        p.setPen(QPen(Qt::white, 2));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(ip, radius, radius);
        p.setPen(QPen(Qt::black, 1));
        p.drawEllipse(ip, radius - 1, radius - 1);
    }

    // ── 色相滑块 ──
    drawHueSlider(p);

    // ── 透明度滑块 ──
    drawAlphaSlider(p);
}

void HQColorWidget::drawHueSlider(QPainter &p)
{
    const int w = m_hueRect.width();

    // 色相渐变带：红 → 黄 → 绿 → 青 → 蓝 → 紫 → 红
    QLinearGradient grad(m_hueRect.left(), 0, m_hueRect.right(), 0);
    const int segments = 6;
    for (int i = 0; i <= segments; ++i) {
        const qreal t = qreal(i) / segments;
        grad.setColorAt(t, QColor::fromHsv(int(360 * t), 255, 255));
    }
    p.fillRect(m_hueRect, grad);

    // 边框
    p.setPen(QColor(0x55, 0x55, 0x55));
    p.drawRect(m_hueRect.adjusted(0, 0, -1, -1));

    // 把手
    const int knobX = m_hueRect.left() + m_hue * w / 360;
    drawKnob(p, QPoint(knobX, m_hueRect.center().y()));
}

void HQColorWidget::drawAlphaSlider(QPainter &p)
{
    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

    const QColor base = QColor::fromHsv(m_hue, m_sat, m_val);
    const QRect  &ar  = m_alphaRect;

    // 棋盘格背景（表示透明区域）
    const int checkSize = DPR_INT(4 * scale, dpr, 0);
    for (int y = ar.top(); y < ar.bottom(); y += checkSize) {
        for (int x = ar.left(); x < ar.right(); x += checkSize) {
            const bool light = ((x - ar.left()) / checkSize
                              + (y - ar.top())  / checkSize) % 2 == 0;
            p.fillRect(x, y, checkSize, checkSize,
                       light ? QColor(0xCC, 0xCC, 0xCC)
                             : QColor(0x88, 0x88, 0x88));
        }
    }

    // 透明度渐变叠加（左侧透明 → 右侧不透明）
    QLinearGradient grad(ar.left(), 0, ar.right(), 0);
    grad.setColorAt(0, QColor(base.red(), base.green(), base.blue(), 0));
    grad.setColorAt(1, QColor(base.red(), base.green(), base.blue(), 255));
    p.fillRect(ar, grad);

    // 边框
    p.setPen(QColor(0x55, 0x55, 0x55));
    p.drawRect(ar.adjusted(0, 0, -1, -1));

    // 把手
    const int knobX = ar.left() + m_alpha * ar.width() / 255;
    drawKnob(p, QPoint(knobX, ar.center().y()));
}

void HQColorWidget::drawKnob(QPainter &p, const QPoint &pos) const
{
    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

    int knobRadius = DPR_INT(kKnobRadius * scale, dpr, 0);
    // 白色外圈
    p.setPen(QPen(Qt::white, 2));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(pos, knobRadius + 1, knobRadius + 1);
    // 黑色内圈（在暗色背景上可见）
    p.setPen(QPen(Qt::black, 1));
    p.drawEllipse(pos, knobRadius, knobRadius);
}

// ─── 鼠标事件 ──────────────────────────────────────────────────────
void HQColorWidget::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;

    const QPoint pos = e->pos();

    if (m_pickerRect.contains(pos)) {
        m_draggingPicker = true;
    } else if (m_hueRect.contains(pos)) {
        m_draggingHue = true;
    } else if (m_alphaRect.contains(pos)) {
        m_draggingAlpha = true;
    } else {
        return;
    }

    // 按下时立即更新值到点击位置
    QMouseEvent synth(QEvent::MouseMove, pos, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    mouseMoveEvent(&synth);
}

void HQColorWidget::mouseMoveEvent(QMouseEvent *e)
{
    const QPoint pos = e->pos();
    bool changed = false;

    if (m_draggingPicker) {
        const QPoint local = pos - m_pickerRect.topLeft();
        const int sat = qBound(0, local.x() * 255 / m_pickerRect.width(), 255);
        const int val = qBound(0, 255 - local.y() * 255 / m_pickerRect.height(), 255);
        if (sat != m_sat || val != m_val) {
            m_sat = sat;
            m_val = val;
            changed = true;
        }
    } else if (m_draggingHue) {
        const int hue = qBound(0, (pos.x() - m_hueRect.left()) * 360 / m_hueRect.width(), 359);
        if (hue != m_hue) {
            m_hue = hue;
            updatePickerPixmap();
            changed = true;
        }
    } else if (m_draggingAlpha) {
        const int alpha = qBound(0, (pos.x() - m_alphaRect.left()) * 255 / m_alphaRect.width(), 255);
        if (alpha != m_alpha) {
            m_alpha = alpha;
            changed = true;
        }
    }

    if (changed) {
        update();
        emit colorChanged(currentColor());
    }
}

void HQColorWidget::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;

    const bool wasDragging = m_draggingPicker || m_draggingHue || m_draggingAlpha;

    m_draggingPicker = false;
    m_draggingHue    = false;
    m_draggingAlpha  = false;

    if (wasDragging)
        emit colorSelected(currentColor());
}

// ─── 辅助 ──────────────────────────────────────────────────────────
QPoint HQColorWidget::indicatorPos(const QColor &color) const
{
    const int px = m_pickerRect.left()
                 + qMin(color.saturation() * m_pickerRect.width()  / 255, m_pickerRect.width()  - 1);
    const int py = m_pickerRect.top()
                 + qMin((255 - color.value()) * m_pickerRect.height() / 255, m_pickerRect.height() - 1);
    return QPoint(px, py);
}
