#include "ProgressBarWidget.h"
#include "comm/ScreenScale.h"
#include "comm/CommonMacros.h"

#include <QPainter>
#include <QStyle>

// ============================================================
// 构造
// ============================================================

ProgressBarWidget::ProgressBarWidget(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("progressBarWidget"));

    // 水平方向 Expanding：在布局中占据剩余空间
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    // ---- 进度动画 ----
    m_animation = new QVariantAnimation(this);
    m_animation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_animation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        m_value = value.toInt();
        update();
    });
    connect(m_animation, &QVariantAnimation::finished, this, [this]() {
        m_value = m_targetValue;
        update();
    });
}

// ============================================================
// Setter 方法
// ============================================================

void ProgressBarWidget::setIcon(const QPixmap &pixmap, int iconSize)
{
    m_icon = pixmap;
    m_iconSize = iconSize;
    update();
    updateGeometry();
}

void ProgressBarWidget::setLabel(const QString &text)
{
    m_label = text;
    update();
    updateGeometry();
}

void ProgressBarWidget::setValue(int percent)
{
    m_targetValue = qBound(0, percent, 100);

    // 已在目标值 → 跳过动画
    if (m_targetValue == m_value && m_animation->state() != QAbstractAnimation::Running)
        return;

    // 取当前动画中间值作为起点（支持动画中途重新设值）
    const int startValue = m_value;

    m_animation->stop();
    m_animation->setStartValue(startValue);
    m_animation->setEndValue(m_targetValue);

    // 动画时长与差值成正比：每 1% 约 3ms，最短 150ms，最长 600ms
    const int diff = qAbs(m_targetValue - startValue);
    m_animation->setDuration(qBound(150, (diff + 50) * 6, 600));
    m_animation->start();
}

void ProgressBarWidget::setBarFont(const QFont &font)
{
    m_font = font;
    update();
    updateGeometry();
}

void ProgressBarWidget::setBarHeight(int heightPx)
{
    m_barHeight = qMax(1, heightPx);
    update();
}

void ProgressBarWidget::setBarBgColor(const QColor &color)
{
    m_barBgColor = color;
    update();
}

void ProgressBarWidget::setBarFillColor(const QColor &color)
{
    m_barFillColor = color;
    update();
}

void ProgressBarWidget::setTextColor(const QColor &color)
{
    m_textColor = color;
    update();
}

// ============================================================
// sizeHint
// ============================================================

QSize ProgressBarWidget::sizeHint() const
{
    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

    QFontMetrics fm(m_font);
    const int textW = fm.horizontalAdvance(m_label + QStringLiteral("100%"));
    const int iconS = DPR_INT(m_iconSize * scale, dpr, 0);
    const int totalW = iconS + DPR_INT(8 * scale, dpr, 0) + textW + DPR_INT(120 * scale, dpr, 0);

    return QSize(totalW, DPR_INT(32 * scale, dpr, 0));
}

// ============================================================
// 绘制事件
// ============================================================

void ProgressBarWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

    const int h        = height();
    const int iconS    = DPR_INT(m_iconSize * scale, dpr, 0);
    const int barH     = DPR_INT(m_barHeight * scale, dpr, 0);
    const int spacing1 = DPR_INT(6 * scale, dpr, 0);   // 图标与文字间距
    const int spacing2 = DPR_INT(8 * scale, dpr, 0);   // 文字与进度条间距
    const int spacing3 = DPR_INT(6 * scale, dpr, 0);   // 进度条与百分比间距
    const int yCenter  = h / 2;

    // ---- 1. 图标 ----
    if (!m_icon.isNull()) {
        // 直接设置 DPR，让 drawPixmap 自动处理缩放
        QPixmap src = m_icon;
        src.setDevicePixelRatio(dpr);
        const QRect iconRect(0, yCenter - iconS / 2, iconS, iconS);
        p.drawPixmap(iconRect, src);
    }

    int x = iconS + spacing1;

    // ---- 2. 文字标签 ----
    p.setFont(m_font);
    p.setPen(m_textColor);
    QFontMetrics fm(m_font);
    const int labelWidth = fm.horizontalAdvance(m_label);
    const QRect labelRect(x, 0, labelWidth, h);
    p.drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter, m_label);

    x += labelWidth + spacing2;

    // ---- 3. 进度条 ----
    const QString pctText = QStringLiteral("%1%").arg(m_value);
    const int pctWidth    = fm.horizontalAdvance(pctText);
    const int barRight    = width() - pctWidth - spacing3;
    const int barWidth    = barRight - x;

    if (barWidth > 0 && barH > 0) {
        const int barY      = yCenter - barH / 2;
        const int barRadius = barH / 2;

        // 进度条背景（圆角矩形）
        p.setPen(Qt::NoPen);
        p.setBrush(m_barBgColor);
        p.drawRoundedRect(x, barY, barWidth, barH, barRadius, barRadius);

        // 进度条填充（圆角矩形，按 m_value 裁剪宽度）
        const int fillW = qMax(0, barWidth * m_value / 100);
        if (fillW > 0) {
            p.setBrush(m_barFillColor);
            p.drawRoundedRect(x, barY, fillW, barH, barRadius, barRadius);
        }
    }

    // ---- 4. 百分比文字（使用填充色） ----
    const QRect pctRect(barRight + spacing3, 0, pctWidth, h);
    p.setPen(m_barFillColor);
    p.drawText(pctRect, Qt::AlignLeft | Qt::AlignVCenter, pctText);
}
