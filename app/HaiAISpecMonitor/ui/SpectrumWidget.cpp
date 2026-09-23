#include "SpectrumWidget.h"

#include <QAction>
#include <QEnterEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QMenu>
#include <QMetaType>
#include <QMouseEvent>
#include <QPushButton>
#include <QThread>
#include <QTimer>
#include <QVariantAnimation>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace scn::app
{

namespace
{
QString formatFrequencyLabel(double hz)
{
    const double absHz = std::abs(hz);
    if (absHz >= 1e9)
        return QStringLiteral("%1 GHz").arg(hz / 1e9, 0, 'f', 3);
    if (absHz >= 1e6)
        return QStringLiteral("%1 MHz").arg(hz / 1e6, 0, 'f', 3);
    if (absHz >= 1e3)
        return QStringLiteral("%1 kHz").arg(hz / 1e3, 0, 'f', 3);
    return QStringLiteral("%1 Hz").arg(hz, 0, 'f', 0);
}
}

SpectrumWidget::SpectrumWidget(QWidget* parent)
    : QWidget(parent)
{
    qRegisterMetaType<SpectrumRenderResult>("scn::app::SpectrumRenderResult");
    setMinimumHeight(220);
    setAutoFillBackground(false);
    setAttribute(Qt::WA_NativeWindow, true);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_PaintOnScreen, true);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::CrossCursor);

    m_renderThread = new QThread(this);
    m_renderWorker = new SpectrumRenderWorker;
    m_renderWorker->moveToThread(m_renderThread);
    connect(m_renderThread, &QThread::finished,
            m_renderWorker, &QObject::deleteLater);
    connect(m_renderWorker, &SpectrumRenderWorker::rendered, this,
            [this](const SpectrumRenderResult& result) {
                acceptRenderResult(result);
            }, Qt::QueuedConnection);
    m_renderThread->start();

    m_renderSettleTimer = new QTimer(this);
    m_renderSettleTimer->setSingleShot(true);
    m_renderSettleTimer->setInterval(60);
    connect(m_renderSettleTimer, &QTimer::timeout,
            this, &SpectrumWidget::requestFullRender);

    buildTraceToolbar();
}

SpectrumWidget::~SpectrumWidget()
{
    if (m_renderThread) {
        m_renderThread->quit();
        m_renderThread->wait();
    }
    m_renderWorker = nullptr;
}

QPaintEngine* SpectrumWidget::paintEngine() const
{
    return nullptr;
}

void SpectrumWidget::setSnapshot(const algorithm::DisplaySnapshotPtr& snapshot)
{
    if (m_snapshot == snapshot) return;
    if (m_snapshot && snapshot &&
        m_snapshot->detection.generation != snapshot->detection.generation) {
        clear();
    }
    if (m_snapshot && snapshot) {
        const auto& previous = m_snapshot->frame;
        const auto& next = snapshot->frame;
        const bool sameFrame = m_snapshot->detection.generation == snapshot->detection.generation &&
            previous.sequence == next.sequence && previous.timestampNs == next.timestampNs &&
            previous.sourceName == next.sourceName &&
            previous.startFrequencyHz == next.startFrequencyHz && previous.binWidthHz == next.binWidthHz &&
            previous.resolutionBandwidthHz == next.resolutionBandwidthHz &&
            (previous.referenceLevelDbm == next.referenceLevelDbm ||
             (std::isnan(previous.referenceLevelDbm) && std::isnan(next.referenceLevelDbm))) &&
            previous.powerDb.size() == next.powerDb.size() &&
            (next.powerDb.empty() || std::memcmp(previous.powerDb.data(), next.powerDb.data(),
                                                next.powerDb.size() * sizeof(float)) == 0);
        if (sameFrame) {
            m_snapshot = snapshot;
            update(); // New detections only; keep geometry and the 100-frame traces.
            return;
        }
    }
    m_snapshot = snapshot;
    m_renderSnapshot = snapshot;
    if (m_snapshot && std::isfinite(m_snapshot->frame.referenceLevelDbm)) {
        m_displayStartHz = m_snapshot->frame.startFrequencyHz;
        m_displayEndHz = m_snapshot->frame.endFrequencyHz();
        m_hasDisplayDomain = m_displayEndHz > m_displayStartHz;
        const double newDisplayMaxDb = m_snapshot->frame.referenceLevelDbm;
        const bool displayRangeChanged =
            std::abs(newDisplayMaxDb - m_displayMaxDb) > 0.01;
        m_displayMaxDb = newDisplayMaxDb;
        m_displayMinDb = m_displayMaxDb - m_dynamicRangeDb;
        if (displayRangeChanged || !m_verticalViewInitialized) {
            m_viewMinDb = m_displayMinDb;
            m_viewMaxDb = m_displayMaxDb;
        }
    }
    if (m_hasSelection && m_snapshot && m_snapshot->frame.isValid()) {
        const auto& frame = m_snapshot->frame;
        m_selectedFrequencyHz = std::clamp(m_selectedFrequencyHz,
                                           frame.startFrequencyHz,
                                           frame.endFrequencyHz());
        m_selectedPowerDb = frame.powerDb[indexAtFrequency(m_selectedFrequencyHz)];
    }
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
    const bool interactivePreview = m_renderSettleTimer && m_renderSettleTimer->isActive();
    submitRenderRequest(interactivePreview);
    update();
}

void SpectrumWidget::setPolicySnapshot(const application::policy::PolicySnapshotPtr& snapshot)
{
    if (m_policySnapshot == snapshot) return;
    m_policySnapshot = snapshot;
    update();
}

void SpectrumWidget::setDisplayDomain(double startHz, double endHz,
                                      double referenceLevelDbm)
{
    if (!std::isfinite(startHz) || !std::isfinite(endHz) || !(endHz > startHz)) {
        return;
    }
    m_displayStartHz = startHz;
    m_displayEndHz = endHz;
    m_hasDisplayDomain = true;
    if (std::isfinite(referenceLevelDbm)) {
        m_displayMaxDb = referenceLevelDbm;
        m_displayMinDb = referenceLevelDbm - m_dynamicRangeDb;
    }
    m_viewMinDb = m_displayMinDb;
    m_viewMaxDb = m_displayMaxDb;
    m_verticalViewInitialized = false;

    if (!m_snapshot || !m_snapshot->frame.isValid()) {
        m_viewStartHz = startHz;
        m_viewEndHz = endHz;
        m_viewInitialized = true;
        m_manualView = false;
    } else if (!m_manualView) {
        m_viewStartHz = startHz;
        m_viewEndHz = endHz;
        m_viewInitialized = true;
    } else {
        const double fullWidth = endHz - startHz;
        const double minimumWidth = std::min(fullWidth, std::max(fullWidth / 100000.0, 1.0));
        const double viewWidth = std::clamp(m_viewEndHz - m_viewStartHz,
                                            minimumWidth, fullWidth);
        const double previousStart = m_viewStartHz;
        const double previousEnd = m_viewEndHz;
        m_viewStartHz = std::clamp(m_viewStartHz, startHz, endHz - viewWidth);
        m_viewEndHz = m_viewStartHz + viewWidth;
        m_viewInitialized = true;
        if (std::abs(previousStart - m_viewStartHz) > 0.01 ||
            std::abs(previousEnd - m_viewEndHz) > 0.01) {
            emit viewRangeChanged(m_viewStartHz, m_viewEndHz);
        }
    }
    ++m_renderGeneration;
    if (m_renderWorker) m_renderWorker->reset(m_renderGeneration);
    submitRenderRequest(true);
    if (m_renderSettleTimer) m_renderSettleTimer->start();
    update();
}

void SpectrumWidget::setDynamicRangeDb(double dynamicRangeDb)
{
    if (!std::isfinite(dynamicRangeDb) || dynamicRangeDb <= 0.0) return;
    const double safeRangeDb = std::clamp(dynamicRangeDb, 1.0, 200.0);
    if (std::abs(safeRangeDb - m_dynamicRangeDb) < 1e-9) return;

    m_dynamicRangeDb = safeRangeDb;
    m_displayMinDb = m_displayMaxDb - m_dynamicRangeDb;
    m_viewMinDb = m_displayMinDb;
    m_viewMaxDb = m_displayMaxDb;
    m_verticalViewInitialized = false;
    // Keep the rolling 100-frame max/average state; only rebuild its display geometry.
    submitRenderRequest(false);
    update();
}

void SpectrumWidget::setFrequencyView(double startHz, double endHz)
{
    stopViewAnimation();
    applyView(startHz, endHz, true);
    // External view synchronization (MainWindow persistence or waterfall
    // coupling) must survive the next incoming frame as a manual view.
    m_manualView = true;
    submitRenderRequest(true);
    if (m_renderSettleTimer) m_renderSettleTimer->start();
}

void SpectrumWidget::resetFrequencyView()
{
    double startHz = 0.0;
    double endHz = 0.0;
    if (!fullRange(startHz, endHz)) return;
    // Navigation restores only the horizontal frequency domain. The current
    // vertical power range remains unchanged.
    animateViewTo(startHz, endHz, false);
}

void SpectrumWidget::setSelectedFrequency(double frequencyHz)
{
    if (!m_snapshot || !m_snapshot->frame.isValid()) return;
    const auto& frame = m_snapshot->frame;
    const double fullStart = frame.startFrequencyHz;
    const double fullEnd = frame.endFrequencyHz();
    if (!(fullEnd > fullStart) || !std::isfinite(frequencyHz)) return;
    m_selectedFrequencyHz = std::clamp(frequencyHz, fullStart, fullEnd);
    const std::size_t index = indexAtFrequency(m_selectedFrequencyHz);
    m_selectedPowerDb = frame.powerDb[index];
    m_hasSelection = true;
    update();
}

void SpectrumWidget::resetView()
{
    m_viewMinDb = m_displayMinDb;
    m_viewMaxDb = m_displayMaxDb;
    m_verticalViewInitialized = false;
    double startHz = 0.0;
    double endHz = 0.0;
    if (!fullRange(startHz, endHz)) return;
    animateViewTo(startHz, endHz, false);
}

void SpectrumWidget::clear()
{
    stopViewAnimation();
    ++m_renderGeneration;
    if (m_renderWorker) m_renderWorker->reset(m_renderGeneration);
    if (m_renderSettleTimer) m_renderSettleTimer->stop();
    m_snapshot.reset();
    m_policySnapshot.reset();
    m_renderSnapshot.reset();
    m_viewInitialized = m_hasDisplayDomain && m_displayEndHz > m_displayStartHz;
    m_viewStartHz = m_displayStartHz;
    m_viewEndHz = m_displayEndHz;
    m_viewMinDb = m_displayMinDb;
    m_viewMaxDb = m_displayMaxDb;
    m_verticalViewInitialized = false;
    m_manualView = false;
    m_hasSelection = false;
    m_currentUpper.clear();
    m_currentLower.clear();
    m_maxUpper.clear();
    m_maxLower.clear();
    m_averageUpper.clear();
    m_averageLower.clear();
    m_latestRenderRequestId = 0;
    m_renderedRequestId = 0;
    m_renderedFrameSequence = 0;
    m_renderedViewStartHz = 0.0;
    m_renderedViewEndHz = 0.0;
    m_renderedDisplayMinDb = m_displayMinDb;
    m_renderedDisplayMaxDb = m_displayMaxDb;
    m_renderedPlotWidth = 0;
    m_renderedPlotHeight = 0;
    update();
}

bool SpectrumWidget::hasFrequencyView() const noexcept
{
    return m_viewInitialized && m_viewEndHz > m_viewStartHz;
}

double SpectrumWidget::viewStartFrequency() const noexcept
{
    return m_viewStartHz;
}

double SpectrumWidget::viewEndFrequency() const noexcept
{
    return m_viewEndHz;
}

bool SpectrumWidget::hasSelectedFrequency() const noexcept
{
    return m_hasSelection;
}

bool SpectrumWidget::realtimeSpectrumVisible() const noexcept
{
    return m_showRealtimeSpectrum;
}

double SpectrumWidget::selectedFrequency() const noexcept
{
    return m_selectedFrequencyHz;
}

bool SpectrumWidget::maxSpectrumVisible() const noexcept
{
    return m_showMaxSpectrum;
}

bool SpectrumWidget::averageSpectrumVisible() const noexcept
{
    return m_showAverageSpectrum;
}

bool SpectrumWidget::detectionMarkersVisible() const noexcept
{
    return m_showDetectionMarkers;
}

void SpectrumWidget::setMaxSpectrumVisible(bool visible)
{
    if (m_maxSpectrumButton) {
        m_maxSpectrumButton->setChecked(visible);
    } else {
        m_showMaxSpectrum = visible;
        submitRenderRequest(false);
        update();
    }
}

void SpectrumWidget::setRealtimeSpectrumVisible(bool visible)
{
    if (m_realtimeSpectrumButton) {
        m_realtimeSpectrumButton->setChecked(visible);
    } else {
        m_showRealtimeSpectrum = visible;
        submitRenderRequest(false);
        update();
    }
}

void SpectrumWidget::setAverageSpectrumVisible(bool visible)
{
    if (m_averageSpectrumButton) {
        m_averageSpectrumButton->setChecked(visible);
    } else {
        m_showAverageSpectrum = visible;
        submitRenderRequest(false);
        update();
    }
}

void SpectrumWidget::setDetectionMarkersVisible(bool visible)
{
    if (m_detectionMarkersButton) {
        m_detectionMarkersButton->setChecked(visible);
    } else {
        m_showDetectionMarkers = visible;
        update();
    }
}

QRectF SpectrumWidget::plotRect() const
{
    // Keep a dedicated margin for both axes.  The previous QRect::adjusted
    // result put the x labels too close to the splitter edge on small windows,
    // making the last labels appear clipped or disappear entirely.
    const qreal left = 74.0;
    const qreal top = 24.0;
    const qreal right = std::max(left + 1.0, static_cast<qreal>(width() - 22));
    const qreal bottom = std::max(top + 1.0, static_cast<qreal>(height() - 42));
    return QRectF(left, top, right - left, bottom - top);
}

bool SpectrumWidget::fullRange(double& startHz, double& endHz) const
{
    if (m_snapshot && m_snapshot->frame.isValid()) {
        startHz = m_snapshot->frame.startFrequencyHz;
        endHz = m_snapshot->frame.endFrequencyHz();
    } else if (m_hasDisplayDomain) {
        startHz = m_displayStartHz;
        endHz = m_displayEndHz;
    } else {
        return false;
    }
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
    if (!frame.isValid() || frame.powerDb.empty()) return 0;
    const double raw = (frequencyHz - frame.startFrequencyHz) / frame.binWidthHz;
    const auto last = frame.powerDb.size() - 1;
    return std::clamp(static_cast<std::size_t>(std::max(0.0, raw)), std::size_t(0), last);
}

void SpectrumWidget::applyView(double startHz, double endHz, bool notify)
{
    double fullStart = 0.0;
    double fullEnd = 0.0;
    if (!fullRange(fullStart, fullEnd)) return;

    const double fullWidth = fullEnd - fullStart;
    if (!std::isfinite(startHz) || !std::isfinite(endHz)) {
        startHz = fullStart;
        endHz = fullEnd;
    } else if (endHz < startHz) {
        std::swap(startHz, endHz);
    }
    const double minimumWidth = std::min(fullWidth,
                                         std::max(fullWidth / 100000.0, 1.0));
    double width = std::clamp(endHz - startHz, minimumWidth, fullWidth);
    const double maximumStart = std::max(fullStart, fullEnd - width);
    startHz = std::clamp(startHz, fullStart, maximumStart);
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
    submitRenderRequest(true);
    if (m_renderSettleTimer) m_renderSettleTimer->start();
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

void SpectrumWidget::panVerticalByPixels(int deltaPixels)
{
    const QRectF plot = plotRect();
    const double currentMinDb = m_verticalViewInitialized
        ? m_viewMinDb : m_displayMinDb;
    const double currentMaxDb = m_verticalViewInitialized
        ? m_viewMaxDb : m_displayMaxDb;
    const double currentRangeDb = currentMaxDb - currentMinDb;
    if (plot.height() <= 0.0 || !(currentRangeDb > 0.0)) return;

    // Moving the Y-axis upward exposes the lower-power part of the spectrum,
    // matching the direction of a vertically dragged ISA amplitude axis.
    const double deltaDb = static_cast<double>(deltaPixels) / plot.height() *
        currentRangeDb;
    m_viewMinDb = currentMinDb + deltaDb;
    m_viewMaxDb = currentMaxDb + deltaDb;
    m_verticalViewInitialized = true;
    submitRenderRequest(true);
    if (m_renderSettleTimer) m_renderSettleTimer->start();
    update();
}

void SpectrumWidget::zoomVerticalAt(const QPointF& position, double steps)
{
    if (std::abs(steps) < 0.01 || !(m_displayMaxDb > m_displayMinDb)) return;

    const double currentMinDb = m_verticalViewInitialized
        ? m_viewMinDb : m_displayMinDb;
    const double currentMaxDb = m_verticalViewInitialized
        ? m_viewMaxDb : m_displayMaxDb;
    const double currentRangeDb = currentMaxDb - currentMinDb;
    if (!(currentRangeDb > 0.0)) return;

    const QRectF plot = plotRect();
    const double ratio = std::clamp(
        (position.y() - plot.top()) / std::max(1.0, plot.height()), 0.0, 1.0);
    const double anchorDb = currentMaxDb - ratio * currentRangeDb;
    const double newRangeDb = std::clamp(
        currentRangeDb * std::pow(0.8, steps), 2.0, 300.0);
    const double newMaxDb = anchorDb + (1.0 - ratio) * newRangeDb;

    m_viewMaxDb = newMaxDb;
    m_viewMinDb = newMaxDb - newRangeDb;
    m_verticalViewInitialized = true;
    submitRenderRequest(true);
    if (m_renderSettleTimer) m_renderSettleTimer->start();
    update();
}

bool SpectrumWidget::isXAxisArea(const QPointF& position) const
{
    const QRectF plot = plotRect();
    return position.x() >= plot.left() && position.x() <= plot.right() &&
        position.y() >= plot.bottom() && position.y() <= height();
}

bool SpectrumWidget::isYAxisArea(const QPointF& position) const
{
    const QRectF plot = plotRect();
    return position.x() >= 0.0 && position.x() < plot.left() &&
        position.y() >= plot.top() && position.y() <= plot.bottom();
}

void SpectrumWidget::updateInteractionCursor(const QPointF& position)
{
    if (m_xAxisPanning || m_yAxisPanning || m_middlePanning) {
        setCursor(Qt::ClosedHandCursor);
    } else if (isXAxisArea(position) || isYAxisArea(position)) {
        setCursor(Qt::OpenHandCursor);
    } else {
        setCursor(Qt::CrossCursor);
    }
}

void SpectrumWidget::selectAt(const QPointF& position)
{
    if (!m_snapshot || !m_snapshot->frame.isValid() || !plotRect().contains(position)) return;
    const double frequencyHz = frequencyAt(position);
    setSelectedFrequency(frequencyHz);
    emit frequencySelected(m_selectedFrequencyHz);
}

void SpectrumWidget::showRestoreMenu(const QPoint& globalPosition)
{
    stopViewAnimation();
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

void SpectrumWidget::stopViewAnimation()
{
    if (!m_viewAnimation) return;
    m_viewAnimation->stop();
    m_viewAnimation->deleteLater();
    m_viewAnimation = nullptr;
}

void SpectrumWidget::buildTraceToolbar()
{
    m_traceToolbar = new QFrame(this);
    m_traceToolbar->setObjectName(QStringLiteral("spectrumTraceToolbar"));
    m_traceToolbar->setAttribute(Qt::WA_StyledBackground, true);

    auto* layout = new QHBoxLayout(m_traceToolbar);
    layout->setContentsMargins(5, 4, 5, 4);
    layout->setSpacing(4);

    const auto addToggle = [this, layout](const QString& text, QPushButton*& target,
                                          bool checked) {
        target = new QPushButton(text, m_traceToolbar);
        target->setObjectName(QStringLiteral("spectrumTraceButton"));
        target->setCheckable(true);
        target->setChecked(checked);
        target->setFixedHeight(26);
        target->setCursor(Qt::PointingHandCursor);
        layout->addWidget(target);
        return target;
    };

    addToggle(QStringLiteral("实时谱"), m_realtimeSpectrumButton, true);
    addToggle(QStringLiteral("最大谱"), m_maxSpectrumButton, false);
    addToggle(QStringLiteral("平均谱"), m_averageSpectrumButton, false);
    addToggle(QStringLiteral("检测标记"), m_detectionMarkersButton, true);

    connect(m_realtimeSpectrumButton, &QPushButton::toggled, this, [this](bool visible) {
        m_showRealtimeSpectrum = visible;
        submitRenderRequest(false);
        update();
    });
    connect(m_maxSpectrumButton, &QPushButton::toggled, this, [this](bool visible) {
        m_showMaxSpectrum = visible;
        submitRenderRequest(false);
        update();
    });
    connect(m_averageSpectrumButton, &QPushButton::toggled, this, [this](bool visible) {
        m_showAverageSpectrum = visible;
        submitRenderRequest(false);
        update();
    });
    connect(m_detectionMarkersButton, &QPushButton::toggled, this, [this](bool visible) {
        m_showDetectionMarkers = visible;
        update();
    });

    m_traceToolbar->adjustSize();
    positionTraceToolbar();
    m_traceToolbar->raise();
    m_traceToolbar->hide();
}

void SpectrumWidget::positionTraceToolbar()
{
    if (!m_traceToolbar) return;
    m_traceToolbar->adjustSize();
    const QRectF plot = plotRect();
    const int x = std::max(0, static_cast<int>(plot.right()) - m_traceToolbar->width() - 8);
    const int y = std::max(0, static_cast<int>(plot.top()) + 6);
    m_traceToolbar->move(x, y);
    m_traceToolbar->raise();
}

void SpectrumWidget::setTraceToolbarVisible(bool visible)
{
    if (!m_traceToolbar) return;
    if (visible) {
        positionTraceToolbar();
        m_traceToolbar->show();
        m_traceToolbar->raise();
    } else {
        m_traceToolbar->hide();
    }
}

void SpectrumWidget::submitRenderRequest(bool interactivePreview)
{
    if (!m_renderWorker || !m_snapshot || !m_snapshot->frame.isValid()) return;

    const QRectF plot = plotRect();
    if (plot.width() <= 0.0 || plot.height() <= 0.0) return;

    SpectrumRenderRequest request;
    // Zoom, resize and trace toggles must also reuse the raw-frame identity after
    // a detection-only publication, otherwise the worker accumulates it again.
    request.snapshot = m_renderSnapshot;
    request.requestId = ++m_nextRenderRequestId;
    request.generation = m_renderGeneration;
    request.viewStartHz = m_viewInitialized
        ? m_viewStartHz : m_snapshot->frame.startFrequencyHz;
    request.viewEndHz = m_viewInitialized
        ? m_viewEndHz : m_snapshot->frame.endFrequencyHz();
    request.displayMinDb = m_displayMinDb;
    request.displayMaxDb = m_displayMaxDb;
    request.plotWidth = std::max(1, static_cast<int>(std::floor(plot.width())));
    request.plotHeight = std::max(1, static_cast<int>(std::floor(plot.height())));
    request.showMaxSpectrum = m_showMaxSpectrum;
    request.showAverageSpectrum = m_showAverageSpectrum;
    request.showCurrentSpectrum = m_showRealtimeSpectrum;
    request.interactivePreview = interactivePreview;
    m_latestRenderRequestId = request.requestId;
    m_renderWorker->submit(request);
}

void SpectrumWidget::requestFullRender()
{
    submitRenderRequest(false);
}

void SpectrumWidget::acceptRenderResult(const SpectrumRenderResult& result)
{
    if (result.generation != m_renderGeneration ||
        result.requestId <= m_renderedRequestId) {
        return;
    }

    m_currentUpper = result.currentUpper;
    m_currentLower = result.currentLower;
    m_maxUpper = result.maxUpper;
    m_maxLower = result.maxLower;
    m_averageUpper = result.averageUpper;
    m_averageLower = result.averageLower;
    m_renderedRequestId = result.requestId;
    m_renderedFrameSequence = result.frameSequence;
    m_renderedViewStartHz = result.viewStartHz;
    m_renderedViewEndHz = result.viewEndHz;
    m_renderedDisplayMinDb = result.displayMinDb;
    m_renderedDisplayMaxDb = result.displayMaxDb;
    m_renderedPlotWidth = result.plotWidth;
    m_renderedPlotHeight = result.plotHeight;
    update();
}

void SpectrumWidget::drawPolylinePair(Direct2DChartRenderer& renderer,
                                      const QPolygonF& upper,
                                      const QPolygonF& lower,
                                      const QColor& color) const
{
    Q_UNUSED(lower)
    // The worker stores min/max samples as one same-X envelope polyline.  Do
    // not draw the legacy second polygon: that produced a duplicate trace and
    // made the plot look as if two independent spectra were being displayed.
    if (upper.isEmpty() || !m_viewInitialized || !(m_viewEndHz > m_viewStartHz)) return;
    const QRectF plot = plotRect();
    QPolygonF mapped;
    const double currentMinDb = m_verticalViewInitialized ? m_viewMinDb : m_displayMinDb;
    const double currentMaxDb = m_verticalViewInitialized ? m_viewMaxDb : m_displayMaxDb;
    const bool sameView = std::abs(m_renderedViewStartHz - m_viewStartHz) < 0.01 &&
        std::abs(m_renderedViewEndHz - m_viewEndHz) < 0.01 &&
        std::abs(m_renderedDisplayMinDb - currentMinDb) < 0.01 &&
        std::abs(m_renderedDisplayMaxDb - currentMaxDb) < 0.01 &&
        m_renderedPlotWidth == static_cast<int>(std::floor(plot.width()));
    if (sameView) {
        renderer.drawPolyline(upper, plot.topLeft(), color, 1.0F);
        return;
    }

    const double sourceWidth = std::max(1, m_renderedPlotWidth - 1);
    const double sourceRange = m_renderedViewEndHz - m_renderedViewStartHz;
    const double currentRange = m_viewEndHz - m_viewStartHz;
    const double renderedDbRange = m_renderedDisplayMaxDb - m_renderedDisplayMinDb;
    const double currentDbRange = currentMaxDb - currentMinDb;
    mapped.reserve(upper.size());
    for (const QPointF& point : upper) {
        const double frequency = m_renderedViewStartHz +
            std::clamp(point.x() / sourceWidth, 0.0, 1.0) * sourceRange;
        const double x = std::clamp((frequency - m_viewStartHz) / currentRange,
                                    -0.25, 1.25) * plot.width();
        const double valueDb = renderedDbRange > 0.0
            ? m_renderedDisplayMaxDb -
                (point.y() / std::max(1, m_renderedPlotHeight)) * renderedDbRange
            : m_renderedDisplayMaxDb;
        const double y = currentDbRange > 0.0
            ? (currentMaxDb - valueDb) / currentDbRange * plot.height()
            : 0.0;
        mapped.append(QPointF(x, y));
    }
    if (!mapped.isEmpty()) renderer.drawPolyline(mapped, plot.topLeft(), color, 1.0F);
}

void SpectrumWidget::drawDetectionMarkers(Direct2DChartRenderer& renderer,
                                          const QRectF& plot,
                                          double viewStartHz, double viewEndHz) const
{
    if (!m_showDetectionMarkers || !m_snapshot || !m_policySnapshot ||
        !m_snapshot->frame.isValid() ||
        m_policySnapshot->generation != m_snapshot->detection.generation ||
        m_policySnapshot->detectionConfigVersion != m_snapshot->detection.configVersion ||
        m_policySnapshot->sequence > m_snapshot->frame.sequence) return;
    const double viewWidthHz = std::max(1.0, viewEndHz - viewStartHz);
    const double fullStartHz = m_snapshot->frame.startFrequencyHz;
    const double fullEndHz = m_snapshot->frame.endFrequencyHz();

    for (const auto& businessSignal : m_policySnapshot->businessSignals) {
        const auto& detection = businessSignal.measurement;
        const double startHz = std::max({detection.startFrequencyHz, fullStartHz, viewStartHz});
        const double endHz = std::min({detection.endFrequencyHz, fullEndHz, viewEndHz});
        if (!(endHz > startHz)) continue;

        const QColor color = businessSignal.source == application::policy::PolicySignalSource::Whitelist
            ? QColor(255, 190, 48) : QColor(10, 140, 254);
        const qreal left = plot.left() + plot.width() * (startHz - viewStartHz) / viewWidthHz;
        const qreal right = plot.left() + plot.width() * (endHz - viewStartHz) / viewWidthHz;
        QRectF marker(left, plot.top() + 5.0,
                      std::max<qreal>(3.0, right - left), plot.height() - 10.0);
        renderer.fillRect(marker, QColor(color.red(), color.green(), color.blue(), 24));
        renderer.drawRect(marker, color, 1.0F);
        renderer.drawText(QString::fromStdString(businessSignal.displayId),
                          QRectF(marker.left() + 4.0, marker.top() + 3.0,
                                 std::max<qreal>(0.0, marker.width() - 8.0), 17.0),
                          color.lighter(125), 12.0F, Qt::AlignLeft);
    }
}

void SpectrumWidget::animateViewTo(double startHz, double endHz, bool manualViewAfterAnimation)
{
    double fullStartHz = 0.0;
    double fullEndHz = 0.0;
    if (!fullRange(fullStartHz, fullEndHz)) return;

    const double fullWidthHz = fullEndHz - fullStartHz;
    if (!std::isfinite(startHz) || !std::isfinite(endHz)) {
        startHz = fullStartHz;
        endHz = fullEndHz;
    } else if (endHz < startHz) {
        std::swap(startHz, endHz);
    }
    const double minimumWidthHz = std::min(fullWidthHz,
                                           std::max(fullWidthHz / 100000.0, 1.0));
    const double targetWidthHz = std::clamp(endHz - startHz, minimumWidthHz, fullWidthHz);
    const double targetStartHz = std::clamp(
        startHz, fullStartHz, std::max(fullStartHz, fullEndHz - targetWidthHz));
    const double targetEndHz = targetStartHz + targetWidthHz;
    const double initialStartHz = m_viewInitialized ? m_viewStartHz : fullStartHz;
    const double initialEndHz = m_viewInitialized ? m_viewEndHz : fullEndHz;

    stopViewAnimation();
    m_manualView = true;
    if (std::abs(initialStartHz - targetStartHz) < 0.01 &&
        std::abs(initialEndHz - targetEndHz) < 0.01) {
        applyView(targetStartHz, targetEndHz, true);
        m_manualView = manualViewAfterAnimation;
        return;
    }

    auto* animation = new QVariantAnimation(this);
    m_viewAnimation = animation;
    animation->setDuration(300);
    animation->setEasingCurve(QEasingCurve::OutCubic);
    animation->setStartValue(0.0);
    animation->setEndValue(1.0);
    connect(animation, &QVariantAnimation::valueChanged, this,
            [this, initialStartHz, initialEndHz, targetStartHz, targetEndHz](const QVariant& value) {
        const double amount = value.toDouble();
        applyView(initialStartHz + (targetStartHz - initialStartHz) * amount,
                  initialEndHz + (targetEndHz - initialEndHz) * amount, true);
    });
    connect(animation, &QVariantAnimation::finished, this,
            [this, animation, targetStartHz, targetEndHz, manualViewAfterAnimation] {
        if (m_viewAnimation != animation) return;
        applyView(targetStartHz, targetEndHz, true);
        m_manualView = manualViewAfterAnimation;
        m_viewAnimation = nullptr;
        animation->deleteLater();
    });
    animation->start();
}

void SpectrumWidget::paintEvent(QPaintEvent*)
{
    if (!m_direct2D.begin(reinterpret_cast<void*>(winId()), size(), devicePixelRatioF(),
                          QColor(7, 15, 27))) {
        return;
    }
    const QRectF plot = plotRect();
    const double viewMinDb = m_verticalViewInitialized ? m_viewMinDb : m_displayMinDb;
    const double viewMaxDb = m_verticalViewInitialized ? m_viewMaxDb : m_displayMaxDb;
    m_direct2D.fillRect(plot, QColor(7, 24, 43));
    for (int i = 0; i <= 6; ++i) {
        const qreal y = plot.top() + plot.height() * i / 6.0;
        m_direct2D.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y),
                            QColor(29, 57, 87), 1.0F);
    }
    for (int i = 0; i <= 4; ++i) {
        const qreal x = plot.left() + plot.width() * i / 4.0;
        m_direct2D.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()),
                            QColor(29, 57, 87), 1.0F);
    }
    m_direct2D.drawRect(plot, QColor(43, 76, 111), 1.0F);

    m_direct2D.drawText(QStringLiteral("功率谱 (dB)"), QRectF(12, 4, width() - 24, 18),
                        QColor(177, 197, 218), 13.0F, Qt::AlignLeft);

    for (int i = 0; i <= 6; ++i) {
        const double db = viewMaxDb - (viewMaxDb - viewMinDb) * i / 6.0;
        const qreal y = plot.top() + plot.height() * i / 6.0;
        m_direct2D.drawText(QStringLiteral("%1").arg(db, 0, 'f', 0),
                            QRectF(8, y - 9, 58, 18), QColor(177, 197, 218),
                            12.0F, Qt::AlignRight | Qt::AlignVCenter);
    }

    double fullStartHz = 0.0;
    double fullEndHz = 0.0;
    const bool hasRange = fullRange(fullStartHz, fullEndHz);
    const double viewStartHz = m_viewInitialized ? m_viewStartHz : fullStartHz;
    const double viewEndHz = m_viewInitialized ? m_viewEndHz : fullEndHz;
    const double viewWidthHz = std::max(1.0, viewEndHz - viewStartHz);

    if (!hasRange || !(viewEndHz > viewStartHz)) {
        m_direct2D.drawText(QStringLiteral("Waiting for spectrum data..."), plot,
                            QColor(130, 145, 162), 13.0F,
                            Qt::AlignHCenter | Qt::AlignVCenter);
        m_direct2D.end();
        return;
    }

    m_direct2D.pushClip(plot);
    if (m_snapshot && m_snapshot->frame.isValid()) {
        if (m_showRealtimeSpectrum) {
            drawPolylinePair(m_direct2D, m_currentUpper, m_currentLower,
                             QColor(49, 201, 255));
        }

        if (m_showMaxSpectrum) {
            drawPolylinePair(m_direct2D, m_maxUpper, m_maxLower,
                             QColor(170, 170, 170));
        }
        if (m_showAverageSpectrum) {
            drawPolylinePair(m_direct2D, m_averageUpper, m_averageLower,
                             QColor(8, 196, 33));
        }
        drawDetectionMarkers(m_direct2D, plot, viewStartHz, viewEndHz);
    } else {
        m_direct2D.drawText(QStringLiteral("Waiting for spectrum data..."), plot,
                            QColor(130, 145, 162), 13.0F,
                            Qt::AlignHCenter | Qt::AlignVCenter);
    }
    m_direct2D.popClip();

    for (int i = 0; i <= 4; ++i) {
        const double hz = viewStartHz + (viewEndHz - viewStartHz) * i / 4.0;
        const qreal x = plot.left() + plot.width() * i / 4.0;
        const qreal labelWidth = 150.0;
        const qreal labelX = std::clamp(x - labelWidth / 2.0,
                                        0.0, std::max<qreal>(0.0, width() - labelWidth));
        m_direct2D.drawText(formatFrequencyLabel(hz),
                            QRectF(labelX, plot.bottom() + 4.0, labelWidth, 22),
                            QColor(157, 179, 201), 12.0F,
                            Qt::AlignHCenter | Qt::AlignVCenter);
    }

    if (m_hasSelection) {
        const qreal selectedX = plot.left() + plot.width() *
            std::clamp((m_selectedFrequencyHz - viewStartHz) / viewWidthHz, 0.0, 1.0);
        m_direct2D.drawLine(QPointF(selectedX, plot.top()), QPointF(selectedX, plot.bottom()),
                            QColor(255, 232, 122), 1.2F);
        const double selectedNorm = std::clamp(
            (static_cast<double>(m_selectedPowerDb) - viewMinDb) /
                (viewMaxDb - viewMinDb), 0.0, 1.0);
        m_direct2D.drawEllipse(QPointF(selectedX,
                                       plot.bottom() - plot.height() * selectedNorm),
                               4.0F, QColor(255, 232, 122), true);
        m_direct2D.drawText(QStringLiteral("选中 %1  %2 dB")
                                .arg(formatFrequencyLabel(m_selectedFrequencyHz))
                                .arg(m_selectedPowerDb, 0, 'f', 1),
                            QRectF(std::min<qreal>(selectedX + 8, plot.right() - 210),
                                   plot.bottom() - 24, 204, 18),
                            QColor(255, 232, 122), 12.0F, Qt::AlignLeft);
    }

    if (m_zoomSelecting) {
        const int left = std::max(plot.left(), static_cast<qreal>(
            std::min(m_pressPosition.x(), m_currentPosition.x())));
        const int right = std::min(plot.right(), static_cast<qreal>(
            std::max(m_pressPosition.x(), m_currentPosition.x())));
        if (right > left) {
            // ISA uses a horizontal X-range selection: the overlay spans the
            // entire plot height and is rendered as a translucent gray band.
            const QRect selection(left, static_cast<int>(plot.top()),
                                  right - left, static_cast<int>(plot.height()));
            m_direct2D.fillRect(selection, QColor(128, 128, 128, 80));
            m_direct2D.drawRect(selection, QColor(128, 128, 128, 180), 1.0F);
        }
    }
    m_direct2D.end();
}

void SpectrumWidget::enterEvent(QEnterEvent* event)
{
    QWidget::enterEvent(event);
    setTraceToolbarVisible(true);
}

void SpectrumWidget::leaveEvent(QEvent* event)
{
    QWidget::leaveEvent(event);
    // Defer the hit test by one event turn so moving between the chart and
    // its child toolbar does not briefly hide the controls.
    QTimer::singleShot(0, this, [this] {
        const QPoint localPosition = mapFromGlobal(QCursor::pos());
        if (!rect().contains(localPosition)) setTraceToolbarVisible(false);
    });
}

void SpectrumWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    positionTraceToolbar();
    submitRenderRequest(true);
    if (m_renderSettleTimer) m_renderSettleTimer->start();
    update();
}

void SpectrumWidget::mousePressEvent(QMouseEvent* event)
{
    stopViewAnimation();
    if (event->button() == Qt::LeftButton && m_viewInitialized &&
        isXAxisArea(event->position())) {
        setFocus(Qt::MouseFocusReason);
        grabMouse();
        m_xAxisPanning = true;
        m_pressPosition = event->position().toPoint();
        m_currentPosition = m_pressPosition;
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && isYAxisArea(event->position())) {
        setFocus(Qt::MouseFocusReason);
        grabMouse();
        m_yAxisPanning = true;
        m_pressPosition = event->position().toPoint();
        m_currentPosition = m_pressPosition;
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    if (!plotRect().contains(event->position())) {
        QWidget::mousePressEvent(event);
        return;
    }
    if (event->button() == Qt::LeftButton) {
        setFocus(Qt::MouseFocusReason);
        grabMouse();
        m_leftPressed = true;
        m_zoomSelecting = false;
        m_pressPosition = event->position().toPoint();
        m_currentPosition = m_pressPosition;
        setCursor(Qt::CrossCursor);
        event->accept();
        return;
    }
    if (event->button() == Qt::MiddleButton) {
        setFocus(Qt::MouseFocusReason);
        grabMouse();
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
    if (m_xAxisPanning && (event->buttons() & Qt::LeftButton)) {
        const int delta = position.x() - m_currentPosition.x();
        m_currentPosition = position;
        panByPixels(delta);
        event->accept();
        return;
    }
    if (m_yAxisPanning && (event->buttons() & Qt::LeftButton)) {
        const int delta = position.y() - m_currentPosition.y();
        m_currentPosition = position;
        panVerticalByPixels(delta);
        event->accept();
        return;
    }
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
    if (!event->buttons()) updateInteractionCursor(event->position());
    QWidget::mouseMoveEvent(event);
}

void SpectrumWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_xAxisPanning) {
        m_xAxisPanning = false;
        releaseMouse();
        updateInteractionCursor(event->position());
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && m_yAxisPanning) {
        m_yAxisPanning = false;
        releaseMouse();
        updateInteractionCursor(event->position());
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && m_leftPressed) {
        m_currentPosition = event->position().toPoint();
        if (m_zoomSelecting) {
            const QRectF plot = plotRect();
            const QPoint firstPoint(static_cast<int>(std::clamp<double>(m_pressPosition.x(), plot.left(), plot.right())),
                                    static_cast<int>(std::clamp<double>(m_pressPosition.y(), plot.top(), plot.bottom())));
            const QPoint secondPoint(static_cast<int>(std::clamp<double>(m_currentPosition.x(), plot.left(), plot.right())),
                                     static_cast<int>(std::clamp<double>(m_currentPosition.y(), plot.top(), plot.bottom())));
            const double first = frequencyAt(firstPoint);
            const double second = frequencyAt(secondPoint);
            const double selectionWidth = std::abs(second - first);
            double fullStartHz = 0.0;
            double fullEndHz = 0.0;
            const double minimumWidth = fullRange(fullStartHz, fullEndHz)
                ? std::min(fullEndHz - fullStartHz,
                           std::max((fullEndHz - fullStartHz) / 100000.0, 1.0))
                : 1.0;
            if (selectionWidth >= minimumWidth) {
                animateViewTo(std::min(first, second), std::max(first, second));
            }
        } else {
            selectAt(m_currentPosition);
        }
        m_leftPressed = false;
        m_zoomSelecting = false;
        releaseMouse();
        update();
        event->accept();
        return;
    }
    if (event->button() == Qt::MiddleButton) {
        m_middlePanning = false;
        releaseMouse();
        updateInteractionCursor(event->position());
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void SpectrumWidget::wheelEvent(QWheelEvent* event)
{
    double steps = static_cast<double>(event->angleDelta().y()) / 120.0;
    if (std::abs(steps) < 0.01 && !event->pixelDelta().isNull()) {
        steps = static_cast<double>(event->pixelDelta().y()) / 120.0;
    }
    if (isYAxisArea(event->position())) {
        zoomVerticalAt(event->position(), steps);
        event->accept();
        return;
    }
    const bool inFrequencyZoomArea = plotRect().contains(event->position()) ||
        isXAxisArea(event->position());
    if (!inFrequencyZoomArea || !m_viewInitialized) {
        QWidget::wheelEvent(event);
        return;
    }
    if (std::abs(steps) < 0.01) return;
    const double factor = std::pow(0.8, steps);
    const double anchor = frequencyAt(event->position());
    const double newWidth = (m_viewEndHz - m_viewStartHz) * factor;
    const double ratio = (anchor - m_viewStartHz) / (m_viewEndHz - m_viewStartHz);
    stopViewAnimation();
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
