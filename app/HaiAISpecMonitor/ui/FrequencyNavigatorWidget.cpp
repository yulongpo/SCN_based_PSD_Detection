#include "FrequencyNavigatorWidget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace scn::app
{

namespace
{
bool nearlyEqual(double first, double second)
{
    return std::abs(first - second) <= std::max(0.01, std::abs(first + second) * 1.0e-12);
}
}

FrequencyNavigatorWidget::FrequencyNavigatorWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("frequencyNavigator"));
    setMinimumHeight(22);
    setMaximumHeight(22);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);

    m_requestTimer = new QTimer(this);
    m_requestTimer->setSingleShot(true);
    m_requestTimer->setInterval(16);
    connect(m_requestTimer, &QTimer::timeout,
            this, &FrequencyNavigatorWidget::flushPendingRequest);
}

void FrequencyNavigatorWidget::setDomain(double startHz, double endHz)
{
    if (!std::isfinite(startHz) || !std::isfinite(endHz) || !(endHz > startHz)) {
        m_hasDomain = false;
        m_hasView = false;
        update();
        return;
    }

    m_domainStartHz = startHz;
    m_domainEndHz = endHz;
    m_hasDomain = true;
    if (m_interaction != Interaction::None || m_hasPendingRequest) {
        update();
        return;
    }
    if (!m_hasView) {
        m_viewStartHz = startHz;
        m_viewEndHz = endHz;
        m_hasView = true;
    } else {
        normalizeRange(m_viewStartHz, m_viewEndHz);
    }
    update();
}

void FrequencyNavigatorWidget::setViewRange(double startHz, double endHz)
{
    if (!m_hasDomain || !std::isfinite(startHz) || !std::isfinite(endHz)) return;
    if (m_interaction != Interaction::None || m_hasPendingRequest) return;
    if (!normalizeRange(startHz, endHz)) return;
    m_viewStartHz = startHz;
    m_viewEndHz = endHz;
    m_hasView = true;
    update();
}

QRectF FrequencyNavigatorWidget::trackRect() const
{
    const qreal left = 74.0;
    const qreal right = std::max(left + 1.0, static_cast<qreal>(width() - 22));
    return QRectF(left, 5.0, right - left, 10.0);
}

double FrequencyNavigatorWidget::frequencyAtX(double x) const
{
    const QRectF track = trackRect();
    if (!m_hasDomain || track.width() <= 0.0) return m_domainStartHz;
    const double ratio = std::clamp((x - track.left()) / track.width(), 0.0, 1.0);
    return m_domainStartHz + ratio * (m_domainEndHz - m_domainStartHz);
}

double FrequencyNavigatorWidget::xAtFrequency(double frequencyHz) const
{
    const QRectF track = trackRect();
    if (!m_hasDomain || !(m_domainEndHz > m_domainStartHz)) return track.left();
    const double ratio = std::clamp(
        (frequencyHz - m_domainStartHz) / (m_domainEndHz - m_domainStartHz), 0.0, 1.0);
    return track.left() + ratio * track.width();
}

bool FrequencyNavigatorWidget::normalizeRange(double& startHz, double& endHz) const
{
    if (!m_hasDomain || !(m_domainEndHz > m_domainStartHz)) return false;
    if (!std::isfinite(startHz) || !std::isfinite(endHz)) {
        startHz = m_domainStartHz;
        endHz = m_domainEndHz;
    }
    if (endHz < startHz) std::swap(startHz, endHz);
    const double fullWidth = m_domainEndHz - m_domainStartHz;
    const double minimumWidth = std::min(fullWidth, std::max(fullWidth / 100000.0, 1.0));
    const double width = std::clamp(endHz - startHz, minimumWidth, fullWidth);
    startHz = std::clamp(startHz, m_domainStartHz, m_domainEndHz - width);
    endHz = startHz + width;
    return endHz > startHz;
}

void FrequencyNavigatorWidget::requestUserRange(double startHz, double endHz, bool immediate)
{
    if (!normalizeRange(startHz, endHz)) return;
    m_viewStartHz = startHz;
    m_viewEndHz = endHz;
    m_hasView = true;
    m_pendingStartHz = startHz;
    m_pendingEndHz = endHz;
    m_hasPendingRequest = true;
    update();
    if (immediate) {
        if (m_requestTimer) m_requestTimer->stop();
        flushPendingRequest();
    } else if (m_requestTimer && !m_requestTimer->isActive()) {
        m_requestTimer->start();
    }
}

void FrequencyNavigatorWidget::flushPendingRequest()
{
    if (!m_hasPendingRequest) return;
    m_hasPendingRequest = false;
    emit viewRangeRequested(m_pendingStartHz, m_pendingEndHz);
}

void FrequencyNavigatorWidget::updateCursor(const QPointF& position)
{
    if (m_interaction != Interaction::None) {
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (!m_hasDomain) {
        setCursor(Qt::ArrowCursor);
        return;
    }
    const QRectF track = trackRect();
    const double left = xAtFrequency(m_viewStartHz);
    const double right = xAtFrequency(m_viewEndHz);
    if (std::abs(position.x() - left) <= 8.0 || std::abs(position.x() - right) <= 8.0)
        setCursor(Qt::SizeHorCursor);
    else if (position.x() >= left && position.x() <= right)
        setCursor(Qt::OpenHandCursor);
    else if (track.contains(position))
        setCursor(Qt::PointingHandCursor);
    else
        setCursor(Qt::ArrowCursor);
}

void FrequencyNavigatorWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor(5, 12, 24));
    painter.setPen(QColor(28, 53, 79));
    painter.drawLine(0, height() - 1, width(), height() - 1);

    const QRectF track = trackRect();
    painter.setPen(QColor(43, 76, 111));
    painter.setBrush(QColor(10, 27, 47));
    painter.drawRoundedRect(track, 4.0, 4.0);

    if (m_hasDomain && m_hasView) {
        const qreal left = static_cast<qreal>(xAtFrequency(m_viewStartHz));
        const qreal right = static_cast<qreal>(xAtFrequency(m_viewEndHz));
        const QRectF selection(left, track.top(), std::max<qreal>(1.0, right - left), track.height());
        painter.setPen(QColor(20, 158, 255));
        painter.setBrush(QColor(10, 140, 254, 110));
        painter.drawRoundedRect(selection, 4.0, 4.0);
        painter.setBrush(QColor(185, 231, 255));
        painter.drawRoundedRect(QRectF(left - 3.0, track.top() - 4.0, 6.0, track.height() + 8.0), 2.0, 2.0);
        painter.drawRoundedRect(QRectF(right - 3.0, track.top() - 4.0, 6.0, track.height() + 8.0), 2.0, 2.0);
    }

}

void FrequencyNavigatorWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    update();
}

void FrequencyNavigatorWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !m_hasDomain || !m_hasView) {
        QWidget::mousePressEvent(event);
        return;
    }
    const QRectF track = trackRect();
    if (!track.adjusted(-4.0, -8.0, 4.0, 8.0).contains(event->position())) return;
    const double left = xAtFrequency(m_viewStartHz);
    const double right = xAtFrequency(m_viewEndHz);
    const double x = std::clamp(event->position().x(), track.left(), track.right());
    const double handleTolerance = 8.0;
    if (std::abs(x - left) <= handleTolerance && std::abs(x - right) > handleTolerance) {
        m_interaction = Interaction::ResizeStart;
    } else if (std::abs(x - right) <= handleTolerance) {
        m_interaction = Interaction::ResizeEnd;
    } else if (x >= left && x <= right) {
        m_interaction = Interaction::Pan;
    } else {
        const double width = m_viewEndHz - m_viewStartHz;
        const double center = frequencyAtX(x);
        requestUserRange(center - width / 2.0, center + width / 2.0, true);
        event->accept();
        return;
    }
    m_pressX = x;
    m_pressViewStartHz = m_viewStartHz;
    m_pressViewEndHz = m_viewEndHz;
    grabMouse();
    setCursor(Qt::ClosedHandCursor);
    event->accept();
}

void FrequencyNavigatorWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_interaction != Interaction::None && (event->buttons() & Qt::LeftButton)) {
        const QRectF track = trackRect();
        const double x = std::clamp(event->position().x(), track.left(), track.right());
        const double deltaHz = (x - m_pressX) / track.width() * (m_domainEndHz - m_domainStartHz);
        double startHz = m_pressViewStartHz;
        double endHz = m_pressViewEndHz;
        if (m_interaction == Interaction::Pan) {
            startHz += deltaHz;
            endHz += deltaHz;
        } else if (m_interaction == Interaction::ResizeStart) {
            startHz = frequencyAtX(x);
        } else {
            endHz = frequencyAtX(x);
        }
        requestUserRange(startHz, endHz, false);
        event->accept();
        return;
    }
    updateCursor(event->position());
    QWidget::mouseMoveEvent(event);
}

void FrequencyNavigatorWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_interaction != Interaction::None) {
        m_interaction = Interaction::None;
        if (m_requestTimer) m_requestTimer->stop();
        flushPendingRequest();
        releaseMouse();
        updateCursor(event->position());
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void FrequencyNavigatorWidget::wheelEvent(QWheelEvent* event)
{
    if (!m_hasDomain || !m_hasView) {
        event->ignore();
        return;
    }
    double steps = static_cast<double>(event->angleDelta().y()) / 120.0;
    if (std::abs(steps) < 0.01 && !event->pixelDelta().isNull())
        steps = static_cast<double>(event->pixelDelta().y()) / 120.0;
    if (std::abs(steps) < 0.01) return;
    const QRectF track = trackRect();
    const double x = std::clamp(event->position().x(), track.left(), track.right());
    const double left = xAtFrequency(m_viewStartHz);
    const double right = xAtFrequency(m_viewEndHz);
    const double anchor = x >= left && x <= right
        ? frequencyAtX(x) : (m_viewStartHz + m_viewEndHz) / 2.0;
    const double factor = std::pow(0.8, steps);
    const double width = (m_viewEndHz - m_viewStartHz) * factor;
    const double ratio = (anchor - m_viewStartHz) / (m_viewEndHz - m_viewStartHz);
    requestUserRange(anchor - width * ratio, anchor + width * (1.0 - ratio), false);
    event->accept();
}

} // namespace scn::app
