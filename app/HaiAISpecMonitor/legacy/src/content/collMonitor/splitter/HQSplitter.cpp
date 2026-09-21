#include "HQSplitter.h"
#include "comm/CommonMacros.h"
#include "comm/ScreenScale.h"
#include "comm/ThemeManager.h"
#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QVariantAnimation>

HQSplitter::HQSplitter(QWidget *parent)
    : QSplitter(Qt::Vertical, parent)
{
    // handle 实际宽度恒定（按屏幕缩放换算），悬停变粗仅靠绘制实现，不影响两侧 widget 尺寸
    const auto &ss = ScreenScale::instance();
    setHandleWidth(DPR_INT(8.0 * ss.scale(), ss.dpr(), 4));
    // 防止把频谱图/时频图拖到收起，保证两侧 widget 至少保留最小高度
    setChildrenCollapsible(false);
}

QSplitterHandle *HQSplitter::createHandle()
{
    return new HQSplitterHandle(orientation(), this);
}

HQSplitterHandle::HQSplitterHandle(Qt::Orientation orientation, QSplitter *parent)
    : QSplitterHandle(orientation, parent)
{
    // 启用悬停事件（HoverEnter/HoverLeave），用于探测鼠标是否移到分隔条上
    setAttribute(Qt::WA_Hover, true);
}

void HQSplitterHandle::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

    // ---- 颜色与线宽按动画进度插值：常态灰色细线 → 悬停亮蓝粗线 ----
    const qreal t = m_animProgress;
    const QColor c0 = ThemeManager::instance().color(QStringLiteral("CollMonitor.borderColor"));
    const QColor c1(10, 140, 254);
    const QColor lineColor(qRound(c0.red()   + (c1.red()   - c0.red())   * t),
                           qRound(c0.green() + (c1.green() - c0.green()) * t),
                           qRound(c0.blue()  + (c1.blue()  - c0.blue())  * t),
                           qRound(c0.alpha() + (c1.alpha() - c0.alpha()) * t));

    const qreal w0 = DPR_REAL(1.0 * scale, dpr);
    const qreal w1 = DPR_REAL(3.0 * scale, dpr);
    const qreal lineW = w0 + (w1 - w0) * t;     // 线宽仅影响绘制，布局宽度恒定

    // 垂直分裂器：手柄为水平条，分隔线垂直居中且水平贯穿
    QRectF lineRect;
    if (orientation() == Qt::Vertical) {
        lineRect = QRectF(0, (height() - lineW) * 0.5, width(), lineW);
    } else {
        lineRect = QRectF((width() - lineW) * 0.5, 0, lineW, height());
    }
    p.fillRect(lineRect, lineColor);
}

bool HQSplitterHandle::event(QEvent *event)
{
    switch (event->type()) {
    case QEvent::HoverEnter:
        m_hovered = true;
        animateTo(1.0);
        break;
    case QEvent::HoverLeave:
        m_hovered = false;
        if (!m_pressed) {
            animateTo(0.0);
        }
        break;
    default:
        break;
    }
    return QSplitterHandle::event(event);
}

void HQSplitterHandle::mousePressEvent(QMouseEvent *event)
{
    // 拖动过程中保持高亮（即使鼠标短暂移出手柄区域）
    m_pressed = true;
    animateTo(1.0);
    QSplitterHandle::mousePressEvent(event);
}

void HQSplitterHandle::mouseReleaseEvent(QMouseEvent *event)
{
    m_pressed = false;
    if (!m_hovered) {
        animateTo(0.0);
    }
    QSplitterHandle::mouseReleaseEvent(event);
}

void HQSplitterHandle::animateTo(qreal target)
{
    // 停止上一次可能还在运行的动画（从当前进度续接，避免跳变）
    if (m_anim) {
        m_anim->stop();
        m_anim->deleteLater();
        m_anim = nullptr;
    }

    m_anim = new QVariantAnimation(this);
    m_anim->setDuration(160);                       // 160ms 平滑过渡
    m_anim->setEasingCurve(QEasingCurve::OutCubic);
    m_anim->setStartValue(m_animProgress);          // 以当前进度为起点
    m_anim->setEndValue(target);

    connect(m_anim, &QVariantAnimation::valueChanged, this,
            [this](const QVariant &val) {
        m_animProgress = val.toReal();
        update();
    });
    connect(m_anim, &QVariantAnimation::finished, this, [this]() {
        m_anim->deleteLater();
        m_anim = nullptr;
    });
    m_anim->start();
}