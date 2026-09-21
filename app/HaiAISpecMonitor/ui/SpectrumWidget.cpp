#include "SpectrumWidget.h"

#include <QAction>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>

namespace scn::app
{

SpectrumWidget::SpectrumWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(220);
    setAutoFillBackground(false);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
}

void SpectrumWidget::setSnapshot(const algorithm::DisplaySnapshotPtr& snapshot)
{
    if (m_snapshot == snapshot) return;
    m_snapshot = snapshot;
    double startHz = 0.0;
    double endHz = 0.0;
    if (fullRange(startHz, endHz)) {
        if (!m_viewInitialized || !m_manualView) {
            m_viewStartHz = startHz;
            m_viewEndHz = endHz;
            m_viewInitialized = true;
            m_manualView = false;
        } else {
            applyView(m_viewStartHz, m_viewEndHz, false);
        }
    }
    update();
}

void SpectrumWidget::setFrequencyView(double startHz, double endHz)
{
    applyView(startHz, endHz, false);
}

void SpectrumWidget::setSelectedFrequency(double frequencyHz)
{
    if (!m_snapshot || m_snapshot->frame.powerDb.empty()) return;
    const auto& frame = m_snapshot->frame;
    const double fullStart = frame.startFrequencyHz;
    const double fullEnd = frame.endFrequencyHz();
    m_selectedFrequencyHz = std::clamp(frequencyHz, fullStart, fullEnd);
    const std::size_t index = indexAtFrequency(m_selectedFrequencyHz);
    m_selectedPowerDb = frame.powerDb[index];
    m_hasSelection = true;
    update();
}

void SpectrumWidget::resetView()
{
    double startHz = 0.0;
    double endHz = 0.0;
    if (!fullRange(startHz, endHz)) return;
    applyView(startHz, endHz, true);
    m_manualView = false;
}

void SpectrumWidget::clear()
{
    m_snapshot.reset();
    m_viewInitialized = false;
    m_manualView = false;
    m_hasSelection = false;
    update();
}

QRectF SpectrumWidget::plotRect() const
{
    return rect().adjusted(74, 24, -22, -42);
}

bool SpectrumWidget::fullRange(double& startHz, double& endHz) const
{
    if (!m_snapshot || !m_snapshot->frame.isValid()) return false;
    startHz = m_snapshot->frame.startFrequencyHz;
    endHz = m_snapshot->frame.endFrequencyHz();
    return endHz > startHz;
}

double SpectrumWidget::frequencyAt(const QPointF& position) const
{
    const QRectF plot = plotRect();
    if (plot.width() <= 0.0 || !m_viewInitialized) return m_viewStartHz;
    const double ratio = std::clamp((position.x() - plot.left()) / plot.width(), 0.0, 1.0);
    return m_viewStartHz + (m_viewEndHz - m_viewStartHz) * ratio;
}

std::size_t SpectrumWidget::indexAtFrequency(double frequencyHz) const
{
    const auto& frame = m_snapshot->frame;
    const double raw = (frequencyHz - frame.startFrequencyHz) / frame.binWidthHz;
    const auto last = frame.powerDb.size() - 1;
    return std::clamp(static_cast<std::size_t>(std::max(0.0, raw)), std::size_t(0), last);
}

void SpectrumWidget::applyView(double startHz, double endHz, bool notify)
{
    double fullStart = 0.0;
    double fullEnd = 0.0;
    if (!fullRange(fullStart, fullEnd)) {
        m_viewStartHz = startHz;
        m_viewEndHz = endHz;
        m_viewInitialized = endHz > startHz;
        return;
    }

    const double fullWidth = fullEnd - fullStart;
    const double minimumWidth = std::max(fullWidth / 100000.0, 1.0);
    double width = std::clamp(endHz - startHz, minimumWidth, fullWidth);
    startHz = std::clamp(startHz, fullStart, fullEnd - width);
    endHz = startHz + width;
    if (std::abs(m_viewStartHz - startHz) < 0.01 &&
        std::abs(m_viewEndHz - endHz) < 0.01 && m_viewInitialized) {
        return;
    }
    m_viewStartHz = startHz;
    m_viewEndHz = endHz;
    m_viewInitialized = true;
    if (notify) {
        m_manualView = true;
        emit viewRangeChanged(m_viewStartHz, m_viewEndHz);
    }
    update();
}

void SpectrumWidget::panByPixels(int deltaPixels)
{
    const QRectF plot = plotRect();
    if (!m_viewInitialized || plot.width() <= 0.0) return;
    const double deltaHz = -static_cast<double>(deltaPixels) / plot.width() *
        (m_viewEndHz - m_viewStartHz);
    applyView(m_viewStartHz + deltaHz, m_viewEndHz + deltaHz, true);
}

void SpectrumWidget::selectAt(const QPointF& position)
{
    if (!m_snapshot || m_snapshot->frame.powerDb.empty() || !plotRect().contains(position)) return;
    const double frequencyHz = frequencyAt(position);
    setSelectedFrequency(frequencyHz);
    emit frequencySelected(m_selectedFrequencyHz);
}

void SpectrumWidget::showRestoreMenu(const QPoint& globalPosition)
{
    QMenu menu(this);
    QAction* restore = menu.addAction(QStringLiteral("还原"));
    menu.addAction(QStringLiteral("取消"));
    menu.setStyleSheet(QStringLiteral(
        "QMenu { background:#071f3d; color:#D6ECFF; border:1px solid #1A304B; "
        "border-radius:4px; padding:4px; }"
        "QMenu::item { padding:4px 24px; border-radius:3px; }"
        "QMenu::item:selected { background:#0A8CFE; color:#FFFFFF; }"));
    if (menu.exec(globalPosition) == restore) resetView();
}

void SpectrumWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(7, 15, 27));
    painter.setRenderHint(QPainter::Antialiasing, false);

    const QRectF plot = plotRect();
    QLinearGradient background(plot.topLeft(), plot.bottomLeft());
    background.setColorAt(0.0, QColor(9, 34, 61));
    background.setColorAt(1.0, QColor(5, 17, 31));
    painter.fillRect(plot, background);

    painter.setPen(QPen(QColor(29, 57, 87), 1));
    for (int i = 0; i <= 5; ++i) {
        const qreal y = plot.top() + plot.height() * i / 5.0;
        painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
    }
    for (int i = 0; i <= 8; ++i) {
        const qreal x = plot.left() + plot.width() * i / 8.0;
        painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
    }

    painter.setPen(QColor(177, 197, 218));
    painter.drawText(QRectF(12, 4, width() - 24, 18), Qt::AlignLeft,
                     QStringLiteral("功率谱 (dB)"));

    constexpr double minDb = -120.0;
    constexpr double maxDb = 0.0;
    for (int i = 0; i <= 6; ++i) {
        const double db = maxDb - (maxDb - minDb) * i / 6.0;
        const qreal y = plot.top() + plot.height() * i / 6.0;
        painter.drawText(QRectF(8, y - 9, 58, 18), Qt::AlignRight,
                         QStringLiteral("%1").arg(db, 0, 'f', 0));
    }

    if (!m_snapshot || m_snapshot->frame.powerDb.empty()) {
        painter.setPen(QColor(130, 145, 162));
        painter.drawText(plot, Qt::AlignCenter, QStringLiteral("Waiting for spectrum data..."));
        return;
    }

    const auto& frame = m_snapshot->frame;
    const auto& values = frame.powerDb;
    const double fullStartHz = frame.startFrequencyHz;
    const double fullEndHz = frame.endFrequencyHz();
    const double viewStartHz = m_viewInitialized ? m_viewStartHz : fullStartHz;
    const double viewEndHz = m_viewInitialized ? m_viewEndHz : fullEndHz;
    const double viewWidthHz = std::max(1.0, viewEndHz - viewStartHz);
    const int pixelWidth = std::max(1, static_cast<int>(plot.width()));
    const std::size_t firstVisibleIndex = std::clamp(static_cast<std::size_t>(std::max(
        0.0, std::floor((viewStartHz - fullStartHz) / frame.binWidthHz))),
        std::size_t(0), values.size() - 1);
    const std::size_t lastVisibleIndex = std::clamp(static_cast<std::size_t>(std::max(
        1.0, std::ceil((viewEndHz - fullStartHz) / frame.binWidthHz))),
        firstVisibleIndex + 1, values.size());
    const std::size_t visibleCount = lastVisibleIndex - firstVisibleIndex;
    const bool renderDirectPoints = visibleCount <= static_cast<std::size_t>(pixelWidth);
    const int outputPointCount = renderDirectPoints
        ? static_cast<int>(visibleCount) : pixelWidth;
    QPainterPath path;
    std::size_t peakIndex = 0;
    float peakValue = -std::numeric_limits<float>::infinity();
    for (int outputIndex = 0; outputIndex < outputPointCount; ++outputIndex) {
        const std::size_t first = renderDirectPoints
            ? firstVisibleIndex + static_cast<std::size_t>(outputIndex)
            : firstVisibleIndex + static_cast<std::size_t>(
                static_cast<double>(outputIndex) * visibleCount / pixelWidth);
        const std::size_t last = renderDirectPoints
            ? first + 1
            : std::min(lastVisibleIndex, firstVisibleIndex + static_cast<std::size_t>(
                static_cast<double>(outputIndex + 1) * visibleCount / pixelWidth));
        const std::size_t end = std::max(first + 1, last);
        float value = -std::numeric_limits<float>::infinity();
        std::size_t valueIndex = first;
        for (std::size_t index = first; index < end && index < values.size(); ++index) {
            if (values[index] > value) {
                value = values[index];
                valueIndex = index;
            }
        }
        if (value > peakValue) {
            peakValue = value;
            peakIndex = valueIndex;
        }
        const double xRatio = outputPointCount <= 1
            ? 0.0 : static_cast<double>(outputIndex) /
                static_cast<double>(outputPointCount - 1);
        const double normalized = std::clamp((static_cast<double>(value) - minDb) / (maxDb - minDb), 0.0, 1.0);
        const QPointF point(
            plot.left() + plot.width() * xRatio,
            plot.bottom() - plot.height() * normalized);
        if (outputIndex == 0) path.moveTo(point); else path.lineTo(point);
    }
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(49, 201, 255), 1.5));
    painter.drawPath(path);

    const double peakHz = fullStartHz + static_cast<double>(peakIndex) * frame.binWidthHz;
    const double peakRatio = std::clamp((peakHz - viewStartHz) / viewWidthHz, 0.0, 1.0);
    const qreal peakX = plot.left() + plot.width() * peakRatio;
    const double peakNorm = std::clamp((static_cast<double>(peakValue) - minDb) / (maxDb - minDb), 0.0, 1.0);
    const qreal peakY = plot.bottom() - plot.height() * peakNorm;
    painter.setPen(QPen(QColor(245, 190, 47), 1.2, Qt::DashLine));
    painter.drawLine(QPointF(peakX, plot.top()), QPointF(peakX, plot.bottom()));
    painter.setBrush(QColor(245, 190, 47));
    painter.drawEllipse(QPointF(peakX, peakY), 4.0, 4.0);
    painter.setPen(QColor(247, 211, 107));
    painter.drawText(QRectF(std::min<qreal>(peakX + 8, plot.right() - 190), plot.top() + 8, 184, 18),
                     Qt::AlignLeft, QStringLiteral("%1 MHz   %2 dB")
                         .arg(peakHz / 1e6, 0, 'f', 3).arg(peakValue, 0, 'f', 1));

    painter.setPen(QColor(157, 179, 201));
    const double endFrequencyHz = m_snapshot->frame.startFrequencyHz +
        m_snapshot->frame.binWidthHz * static_cast<double>(m_snapshot->frame.powerDb.size());
    for (int i = 0; i <= 4; ++i) {
        const double hz = viewStartHz + (viewEndHz - viewStartHz) * i / 4.0;
        const qreal x = plot.left() + plot.width() * i / 4.0;
        painter.drawText(QRectF(x - 70, height() - 31, 140, 18), Qt::AlignCenter,
                         QStringLiteral("%1 GHz").arg(hz / 1e9, 0, 'f', 3));
    }

    if (m_hasSelection) {
        const qreal selectedX = plot.left() + plot.width() *
            std::clamp((m_selectedFrequencyHz - viewStartHz) / viewWidthHz, 0.0, 1.0);
        painter.setPen(QPen(QColor(255, 232, 122), 1.2, Qt::DashLine));
        painter.drawLine(QPointF(selectedX, plot.top()), QPointF(selectedX, plot.bottom()));
        painter.setBrush(QColor(255, 232, 122));
        const double selectedNorm = std::clamp(
            (static_cast<double>(m_selectedPowerDb) - minDb) / (maxDb - minDb), 0.0, 1.0);
        painter.drawEllipse(QPointF(selectedX, plot.bottom() - plot.height() * selectedNorm), 4.0, 4.0);
        painter.setPen(QColor(255, 232, 122));
        painter.drawText(QRectF(std::min<qreal>(selectedX + 8, plot.right() - 210),
                                plot.bottom() - 24, 204, 18), Qt::AlignLeft,
                         QStringLiteral("选中 %1 MHz  %2 dB")
                             .arg(m_selectedFrequencyHz / 1e6, 0, 'f', 3)
                             .arg(m_selectedPowerDb, 0, 'f', 1));
    }

    if (m_zoomSelecting) {
        QRect selection(m_pressPosition, m_currentPosition);
        selection = selection.intersected(plot.toRect());
        painter.setPen(QPen(QColor(85, 205, 255), 1.0, Qt::DashLine));
        painter.setBrush(QColor(46, 151, 220, 42));
        painter.drawRect(selection);
    }
}

void SpectrumWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    update();
}

void SpectrumWidget::mousePressEvent(QMouseEvent* event)
{
    if (!plotRect().contains(event->position())) {
        QWidget::mousePressEvent(event);
        return;
    }
    if (event->button() == Qt::LeftButton) {
        m_leftPressed = true;
        m_zoomSelecting = false;
        m_pressPosition = event->position().toPoint();
        m_currentPosition = m_pressPosition;
        setCursor(Qt::CrossCursor);
        event->accept();
        return;
    }
    if (event->button() == Qt::MiddleButton) {
        m_middlePanning = true;
        m_pressPosition = event->position().toPoint();
        m_currentPosition = m_pressPosition;
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    if (event->button() == Qt::RightButton) {
        showRestoreMenu(event->globalPosition().toPoint());
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void SpectrumWidget::mouseMoveEvent(QMouseEvent* event)
{
    const QPoint position = event->position().toPoint();
    if (m_leftPressed && (event->buttons() & Qt::LeftButton)) {
        m_currentPosition = position;
        m_zoomSelecting = (m_currentPosition - m_pressPosition).manhattanLength() > 5;
        update();
        event->accept();
        return;
    }
    if (m_middlePanning && (event->buttons() & Qt::MiddleButton)) {
        const int delta = position.x() - m_currentPosition.x();
        m_currentPosition = position;
        panByPixels(delta);
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void SpectrumWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_leftPressed) {
        m_currentPosition = event->position().toPoint();
        if (m_zoomSelecting && plotRect().contains(m_pressPosition) && plotRect().contains(m_currentPosition)) {
            const double first = frequencyAt(m_pressPosition);
            const double second = frequencyAt(m_currentPosition);
            applyView(std::min(first, second), std::max(first, second), true);
        } else {
            selectAt(m_currentPosition);
        }
        m_leftPressed = false;
        m_zoomSelecting = false;
        update();
        event->accept();
        return;
    }
    if (event->button() == Qt::MiddleButton) {
        m_middlePanning = false;
        setCursor(Qt::CrossCursor);
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void SpectrumWidget::wheelEvent(QWheelEvent* event)
{
    if (!plotRect().contains(event->position()) || !m_viewInitialized) {
        QWidget::wheelEvent(event);
        return;
    }
    const double steps = static_cast<double>(event->angleDelta().y()) / 120.0;
    if (std::abs(steps) < 0.01) return;
    const double factor = std::pow(0.8, steps);
    const double anchor = frequencyAt(event->position());
    const double newWidth = (m_viewEndHz - m_viewStartHz) * factor;
    const double ratio = (anchor - m_viewStartHz) / (m_viewEndHz - m_viewStartHz);
    applyView(anchor - newWidth * ratio, anchor + newWidth * (1.0 - ratio), true);
    event->accept();
}

void SpectrumWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        resetView();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

} // namespace scn::app
