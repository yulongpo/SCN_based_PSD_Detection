#include "WaterfallWidget.h"

#include <QAction>
#include <QColor>
#include <QMetaObject>
#include <QMenu>
#include <QMouseEvent>
#include <QMutex>
#include <QPainter>
#include <QPointer>
#include <QThread>
#include <QVector>
#include <QWaitCondition>
#include <QWheelEvent>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <utility>

namespace scn::app
{

namespace
{
constexpr std::size_t kHistoryRows = 100;
constexpr int kRenderOversample = 10;
constexpr QRgb kEmptyPixel = qRgb(8, 13, 22);

enum class RenderMode
{
    Incremental,
    InteractivePreview,
    FullQuality
};

struct WaterfallRenderRequest final
{
    std::uint64_t generation = 0;
    std::uint64_t viewGeneration = 0;
    RenderMode mode = RenderMode::FullQuality;
    int imageWidth = 1;
    int imageHeight = 1;
    std::size_t historyCapacity = kHistoryRows;
    std::size_t historyCount = 0;
    std::uint64_t frameSequence = 0;
    double viewStartHz = 0.0;
    double viewEndHz = 0.0;
    std::vector<algorithm::DisplaySnapshotPtr> rows;
    algorithm::DisplaySnapshotPtr newestSnapshot;
    QImage baseImage;
    std::array<QRgb, 256> palette{};
    QPointer<WaterfallWidget> target;
};

} // namespace

class WaterfallRenderWorker final : public QThread
{
public:
    ~WaterfallRenderWorker() override
    {
        stopAndWait();
    }

    void submit(std::shared_ptr<const WaterfallRenderRequest> request)
    {
        QMutexLocker locker(&m_mutex);
        if (m_stopping) return;
        m_latestGeneration.store(request ? request->generation : 0,
                                 std::memory_order_release);
        m_pending = std::move(request);
        m_condition.wakeOne();
    }

    void cancel(std::uint64_t generation)
    {
        QMutexLocker locker(&m_mutex);
        if (m_stopping) return;
        m_latestGeneration.store(generation, std::memory_order_release);
        m_pending.reset();
        m_condition.wakeOne();
    }

    void stopAndWait()
    {
        {
            QMutexLocker locker(&m_mutex);
            m_stopping = true;
            m_pending.reset();
            m_latestGeneration.fetch_add(1, std::memory_order_acq_rel);
            m_condition.wakeOne();
        }
        if (isRunning()) wait();
    }

protected:
    void run() override
    {
        for (;;) {
            std::shared_ptr<const WaterfallRenderRequest> request;
            {
                QMutexLocker locker(&m_mutex);
                while (!m_stopping && !m_pending) m_condition.wait(&m_mutex);
                if (m_stopping) return;
                request = std::move(m_pending);
            }
            if (!request) continue;

            QImage image = render(*request, m_latestGeneration);
            if (image.isNull()) continue;
            const auto target = request->target;
            if (!target) continue;

            QMetaObject::invokeMethod(target.data(),
                [target,
                 generation = request->generation,
                 viewGeneration = request->viewGeneration,
                 interactivePreview = request->mode == RenderMode::InteractivePreview,
                 historyCount = request->historyCount,
                 frameSequence = request->frameSequence,
                 image = std::move(image)]() mutable {
                    if (target) {
                        target->acceptRenderedImage(generation, viewGeneration,
                                                    interactivePreview, historyCount,
                                                    frameSequence, std::move(image));
                    }
                }, Qt::QueuedConnection);
        }
    }

private:
    static QImage render(const WaterfallRenderRequest& request,
                         const std::atomic<std::uint64_t>& latestGeneration)
    {
        const auto canceled = [&]() {
            return request.generation != latestGeneration.load(std::memory_order_acquire);
        };
        const auto colorTable = [&request]() {
            QVector<QRgb> table;
            table.reserve(static_cast<int>(request.palette.size()));
            table.push_back(kEmptyPixel);
            for (std::size_t index = 0; index < request.palette.size() - 2; ++index) {
                table.push_back(request.palette[index]);
            }
            table.push_back(request.palette[255]);
            return table;
        };
        const auto emptyImage = [&request, &colorTable]() {
            const QSize baseSize = request.mode == RenderMode::Incremental
                ? request.baseImage.size()
                : QSize(std::max(1, request.imageWidth),
                        std::max(1, std::min(request.imageHeight,
                                             static_cast<int>(request.historyCapacity))));
            QImage image(baseSize, QImage::Format_Indexed8);
            image.setColorTable(colorTable());
            image.fill(0);
            return image;
        };
        if (canceled()) return {};
        const auto newestSnapshot = request.mode == RenderMode::Incremental
            ? request.newestSnapshot
            : (request.rows.empty() ? algorithm::DisplaySnapshotPtr{} : request.rows.back());
        if (!newestSnapshot || request.historyCount == 0 ||
            request.viewEndHz <= request.viewStartHz) {
            return emptyImage();
        }

        const auto& newest = newestSnapshot->frame;
        if (newest.powerDb.empty() || newest.binWidthHz <= 0.0) return emptyImage();

        const std::size_t pointCount = newest.powerDb.size();
        const double fullStartHz = newest.startFrequencyHz;
        const double fullEndHz = newest.endFrequencyHz();
        const double firstIndexReal =
            (request.viewStartHz - fullStartHz) / newest.binWidthHz;
        const double lastIndexReal =
            (request.viewEndHz - fullStartHz) / newest.binWidthHz;
        const std::size_t visibleStart = std::clamp(
            static_cast<std::size_t>(std::max(0.0, std::floor(firstIndexReal))),
            std::size_t(0), pointCount - 1);
        const std::size_t visibleEnd = std::clamp(
            static_cast<std::size_t>(std::max(1.0, std::ceil(lastIndexReal))),
            visibleStart + 1, pointCount);
        const std::size_t visibleCount = visibleEnd - visibleStart;
        if (visibleCount == 0 || fullEndHz <= fullStartHz) return emptyImage();

        int displayColumns = 0;
        int renderHeight = 0;
        QImage image;
        if (request.mode == RenderMode::Incremental) {
            if (request.baseImage.isNull() ||
                request.baseImage.format() != QImage::Format_Indexed8 ||
                request.baseImage.width() <= 0 || request.baseImage.height() <= 0) {
                return {};
            }
            image = request.baseImage;
            image.detach();
            displayColumns = image.width();
            renderHeight = image.height();
        } else {
            const std::size_t columnLimit = request.mode == RenderMode::InteractivePreview
                ? static_cast<std::size_t>(request.imageWidth)
                : static_cast<std::size_t>(request.imageWidth) * kRenderOversample;
            displayColumns = std::max(1, static_cast<int>(std::min(
                visibleCount, std::max<std::size_t>(1, columnLimit))));
            renderHeight = std::max(1, std::min(
                request.imageHeight, static_cast<int>(request.historyCapacity)));
            image = QImage(QSize(displayColumns, renderHeight), QImage::Format_Indexed8);
            image.setColorTable(colorTable());
            image.fill(0);
        }

        std::vector<std::size_t> sourceIndices(static_cast<std::size_t>(displayColumns));
        for (int column = 0; column < displayColumns; ++column) {
            sourceIndices[static_cast<std::size_t>(column)] = visibleStart + std::min(
                visibleCount - 1,
                static_cast<std::size_t>(static_cast<double>(column) * visibleCount /
                                         displayColumns));
        }

        const auto paletteIndex = [](float value) {
            if (!std::isfinite(value)) return 0;
            const double normalized = std::clamp(
                (static_cast<double>(value) + 120.0) / 120.0, 0.0, 1.0);
            return 1 + std::clamp(static_cast<int>(normalized * 254.0 + 0.5), 0, 254);
        };
        const auto renderRow = [&](const algorithm::DisplaySnapshotPtr& snapshot,
                                   uchar* destination) {
            if (!snapshot || snapshot->frame.powerDb.empty()) return;
            const auto& values = snapshot->frame.powerDb;
            for (int column = 0; column < displayColumns; ++column) {
                if ((column & 255) == 0 && canceled()) return;
                const std::size_t sourceIndex = sourceIndices[static_cast<std::size_t>(column)];
                if (sourceIndex < values.size()) {
                    destination[column] = static_cast<uchar>(paletteIndex(values[sourceIndex]));
                }
            }
        };

        if (request.mode == RenderMode::Incremental) {
            const auto appendRow = [&](const algorithm::DisplaySnapshotPtr& snapshot) {
                if (renderHeight > 1) {
                    std::memmove(image.scanLine(1), image.constScanLine(0),
                                 static_cast<std::size_t>(image.bytesPerLine()) *
                                     static_cast<std::size_t>(renderHeight - 1));
                }
                std::memset(image.scanLine(0), 0,
                            static_cast<std::size_t>(image.bytesPerLine()));
                renderRow(snapshot, image.scanLine(0));
            };
            if (request.rows.empty()) {
                appendRow(newestSnapshot);
            } else {
                // Rows are ordered oldest -> newest. Appending in this order
                // keeps the newest frame at the top and preserves skipped
                // frames when the renderer was busy.
                for (const auto& snapshot : request.rows) {
                    if (canceled()) return {};
                    appendRow(snapshot);
                }
            }
            return canceled() ? QImage{} : image;
        }

        for (int y = 0; y < image.height(); ++y) {
            const int logicalRow = std::clamp(
                static_cast<int>((static_cast<double>(y) + 0.5) * request.historyCapacity /
                                 renderHeight),
                0, static_cast<int>(request.historyCapacity) - 1);
            if (canceled()) return {};
            // The history vector is ordered oldest -> newest. The newest frame
            // belongs at the top of the waterfall; older frames move downward.
            if (logicalRow >= static_cast<int>(request.historyCount)) continue;
            const int historyIndex = static_cast<int>(request.historyCount) - 1 - logicalRow;
            renderRow(request.rows[static_cast<std::size_t>(historyIndex)],
                      image.scanLine(y));
        }
        if (canceled()) return {};
        return image;
    }

    QMutex m_mutex;
    QWaitCondition m_condition;
    std::shared_ptr<const WaterfallRenderRequest> m_pending;
    std::atomic<std::uint64_t> m_latestGeneration{0};
    bool m_stopping = false;
};

WaterfallWidget::WaterfallWidget(QWidget* parent)
    : QWidget(parent),
      m_renderWorker(std::make_unique<WaterfallRenderWorker>()),
      m_history(kHistoryRows)
{
    setMinimumHeight(260);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);

    struct ColorStop { double position; QColor color; };
    constexpr std::array<ColorStop, 8> stops = {{
        {0.00, QColor(0, 37, 94)},
        {0.14, QColor(0, 58, 135)},
        {0.29, QColor(0, 90, 164)},
        {0.43, QColor(20, 210, 215)},
        {0.57, QColor(0, 164, 205)},
        {0.71, QColor(248, 206, 51)},
        {0.86, QColor(254, 157, 44)},
        {1.00, QColor(254, 104, 41)}
    }};
    for (int index = 0; index < 256; ++index) {
        const double position = static_cast<double>(index) / 255.0;
        std::size_t right = 1;
        while (right < stops.size() - 1 && position > stops[right].position) ++right;
        const auto& leftStop = stops[right - 1];
        const auto& rightStop = stops[right];
        const double amount = (position - leftStop.position) /
            (rightStop.position - leftStop.position);
        const auto channel = [amount](int left, int rightValue) {
            return static_cast<int>(left + (rightValue - left) * amount + 0.5);
        };
        m_palette[static_cast<std::size_t>(index)] = QColor(
            channel(leftStop.color.red(), rightStop.color.red()),
            channel(leftStop.color.green(), rightStop.color.green()),
            channel(leftStop.color.blue(), rightStop.color.blue())).rgba();
    }

    m_renderSettleTimer = new QTimer(this);
    m_renderSettleTimer->setSingleShot(true);
    m_renderSettleTimer->setInterval(60);
    connect(m_renderSettleTimer, &QTimer::timeout,
            this, &WaterfallWidget::requestFullQualityRender);

    ensureImage();
    m_renderWorker->start();
}

WaterfallWidget::~WaterfallWidget()
{
    if (m_renderSettleTimer) m_renderSettleTimer->stop();
    if (m_renderWorker) m_renderWorker->stopAndWait();
}

void WaterfallWidget::ensureImage()
{
    // Rendered images may intentionally use up to pixelWidth * 10 columns
    // for ISA-style waterfall resampling. Do not replace such an image merely
    // because its source size differs from the widget's paint rectangle.
    if (!m_image.isNull()) return;
    const QSize imageSize(std::max(1, static_cast<int>(plotRect().width())),
                          std::max(1, std::min(static_cast<int>(plotRect().height()),
                                               static_cast<int>(kHistoryRows))));
    QVector<QRgb> table;
    table.reserve(static_cast<int>(m_palette.size()));
    table.push_back(kEmptyPixel);
    for (std::size_t index = 0; index < m_palette.size() - 2; ++index) {
        table.push_back(m_palette[index]);
    }
    table.push_back(m_palette[255]);
    m_image = QImage(imageSize, QImage::Format_Indexed8);
    m_image.setColorTable(table);
    m_image.fill(0);
}

void WaterfallWidget::setSnapshot(const algorithm::DisplaySnapshotPtr& snapshot)
{
    if (!snapshot || snapshot->frame.powerDb.empty() || snapshot->frame.binWidthHz <= 0.0) return;

    ensureImage();
    const auto& frame = snapshot->frame;
    const bool sourceChanged = m_historyFrameLength != frame.powerDb.size() ||
        std::abs(m_historyStartFrequencyHz - frame.startFrequencyHz) > 0.5 ||
        std::abs(m_historyBinWidthHz - frame.binWidthHz) > 1e-9;
    if (sourceChanged) {
        ++m_viewGeneration;
        resetHistory();
        m_renderedHistoryCount = 0;
        m_renderedFrameSequence = 0;
        m_renderedInteractivePreview = false;
        m_appliedGeneration = 0;
    }

    if (m_historyFrameLength == 0) {
        m_historyFrameLength = frame.powerDb.size();
        m_historyStartFrequencyHz = frame.startFrequencyHz;
        m_historyBinWidthHz = frame.binWidthHz;
    }

    m_snapshot = snapshot;
    m_history[m_historyWriteIndex] = snapshot;
    m_historyWriteIndex = (m_historyWriteIndex + 1) % kHistoryRows;
    m_historyCount = std::min(kHistoryRows, m_historyCount + 1);

    if (!m_viewInitialized || !m_manualView) {
        m_viewStartHz = frame.startFrequencyHz;
        m_viewEndHz = frame.endFrequencyHz();
        m_viewInitialized = m_viewEndHz > m_viewStartHz;
        m_manualView = false;
    } else {
        applyView(m_viewStartHz, m_viewEndHz, false);
    }
    if (m_renderSettleTimer && m_renderSettleTimer->isActive()) return;
    // Do not restart a full render for every incoming frame while the current
    // request is still running. The completed image will be followed by an
    // incremental update using the newest frame, preventing render starvation.
    if (!sourceChanged && m_appliedGeneration != m_renderGeneration) return;
    if (canIncrementallyRender()) {
        requestRender(false, true);
    } else {
        requestRender(false, false);
    }
}

void WaterfallWidget::setFrequencyView(double startHz, double endHz)
{
    if (applyView(startHz, endHz, false)) beginInteractiveRender();
}

void WaterfallWidget::setSelectedFrequency(double frequencyHz)
{
    double fullStart = 0.0;
    double fullEnd = 0.0;
    if (!fullRange(fullStart, fullEnd)) return;
    m_selectedFrequencyHz = std::clamp(frequencyHz, fullStart, fullEnd);
    m_hasSelection = true;
    update();
}

void WaterfallWidget::resetView()
{
    double startHz = 0.0;
    double endHz = 0.0;
    if (!fullRange(startHz, endHz)) return;
    if (applyView(startHz, endHz, true)) beginInteractiveRender();
    m_manualView = false;
}

void WaterfallWidget::clear()
{
    m_snapshot.reset();
    resetHistory();
    m_viewInitialized = false;
    m_manualView = false;
    m_hasSelection = false;
    ++m_renderGeneration;
    ++m_viewGeneration;
    if (m_renderSettleTimer) m_renderSettleTimer->stop();
    if (m_renderWorker) m_renderWorker->cancel(m_renderGeneration);
    m_appliedGeneration = 0;
    m_renderedHistoryCount = 0;
    m_renderedFrameSequence = 0;
    m_renderedInteractivePreview = false;
    ensureImage();
    m_image.fill(0);
    update();
}

void WaterfallWidget::acceptRenderedImage(std::uint64_t generation,
                                          std::uint64_t viewGeneration,
                                          bool interactivePreview,
                                          std::size_t historyCount,
                                          std::uint64_t frameSequence,
                                          QImage image)
{
    if (generation != m_renderGeneration || viewGeneration != m_viewGeneration ||
        image.isNull()) return;
    m_appliedGeneration = generation;
    m_image = std::move(image);
    m_renderedViewStartHz = m_viewStartHz;
    m_renderedViewEndHz = m_viewEndHz;
    m_renderedImageWidth = m_image.width();
    m_renderedImageHeight = m_image.height();
    m_renderedHistoryCount = historyCount;
    m_renderedFrameSequence = frameSequence;
    m_renderedInteractivePreview = interactivePreview;
    update();
}

QRectF WaterfallWidget::plotRect() const
{
    return rect().adjusted(74, 24, -22, -8);
}

bool WaterfallWidget::fullRange(double& startHz, double& endHz) const
{
    if (!m_snapshot || !m_snapshot->frame.isValid()) return false;
    startHz = m_snapshot->frame.startFrequencyHz;
    endHz = m_snapshot->frame.endFrequencyHz();
    return endHz > startHz;
}

double WaterfallWidget::frequencyAt(const QPointF& position) const
{
    const QRectF plot = plotRect();
    if (plot.width() <= 0.0 || !m_viewInitialized) return m_viewStartHz;
    const double ratio = std::clamp((position.x() - plot.left()) / plot.width(), 0.0, 1.0);
    return m_viewStartHz + (m_viewEndHz - m_viewStartHz) * ratio;
}

bool WaterfallWidget::applyView(double startHz, double endHz, bool notify)
{
    double fullStart = 0.0;
    double fullEnd = 0.0;
    if (!fullRange(fullStart, fullEnd)) {
        m_viewStartHz = startHz;
        m_viewEndHz = endHz;
        m_viewInitialized = endHz > startHz;
        ++m_viewGeneration;
        return true;
    }

    const double fullWidth = fullEnd - fullStart;
    const double minimumWidth = std::max(fullWidth / 100000.0, 1.0);
    const double width = std::clamp(endHz - startHz, minimumWidth, fullWidth);
    startHz = std::clamp(startHz, fullStart, fullEnd - width);
    endHz = startHz + width;
    if (std::abs(m_viewStartHz - startHz) < 0.01 &&
        std::abs(m_viewEndHz - endHz) < 0.01 && m_viewInitialized) {
        return false;
    }
    m_viewStartHz = startHz;
    m_viewEndHz = endHz;
    m_viewInitialized = true;
    ++m_viewGeneration;
    if (notify) {
        m_manualView = true;
        emit viewRangeChanged(m_viewStartHz, m_viewEndHz);
    }
    update();
    return true;
}

void WaterfallWidget::panByPixels(int deltaPixels)
{
    const QRectF plot = plotRect();
    if (!m_viewInitialized || plot.width() <= 0.0) return;
    const double deltaHz = -static_cast<double>(deltaPixels) / plot.width() *
        (m_viewEndHz - m_viewStartHz);
    if (applyView(m_viewStartHz + deltaHz, m_viewEndHz + deltaHz, true)) {
        beginInteractiveRender();
    }
}

void WaterfallWidget::selectAt(const QPointF& position)
{
    if (!m_snapshot || !plotRect().contains(position)) return;
    m_hasSelection = true;
    m_selectedFrequencyHz = frequencyAt(position);
    emit frequencySelected(m_selectedFrequencyHz);
    update();
}

void WaterfallWidget::showRestoreMenu(const QPoint& globalPosition)
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

void WaterfallWidget::requestRender(bool interactivePreview, bool incremental)
{
    if (!m_renderWorker || !m_snapshot || m_historyCount == 0 || m_historyFrameLength == 0) return;

    if (incremental && !canIncrementallyRender()) incremental = false;

    auto request = std::make_shared<WaterfallRenderRequest>();
    request->generation = ++m_renderGeneration;
    request->viewGeneration = m_viewGeneration;
    request->mode = incremental
        ? RenderMode::Incremental
        : (interactivePreview ? RenderMode::InteractivePreview : RenderMode::FullQuality);
    request->imageWidth = std::max(1, static_cast<int>(plotRect().width()));
    request->imageHeight = std::max(1, static_cast<int>(plotRect().height()));
    request->historyCapacity = kHistoryRows;
    request->historyCount = m_historyCount;
    request->frameSequence = m_snapshot->frame.sequence;
    request->viewStartHz = m_viewStartHz;
    request->viewEndHz = m_viewEndHz;
    request->palette = m_palette;
    request->target = QPointer<WaterfallWidget>(this);

    if (incremental) {
        request->newestSnapshot = m_snapshot;
        request->baseImage = m_image;
        request->rows.reserve(m_historyCount);
        const std::size_t oldest = (m_historyWriteIndex + kHistoryRows - m_historyCount) % kHistoryRows;
        for (std::size_t index = 0; index < m_historyCount; ++index) {
            const auto& snapshot = m_history[(oldest + index) % kHistoryRows];
            if (snapshot && snapshot->frame.sequence > m_renderedFrameSequence) {
                request->rows.push_back(snapshot);
            }
        }
        if (request->rows.empty()) request->rows.push_back(m_snapshot);
    } else {
        request->rows.reserve(m_historyCount);
        const std::size_t oldest = (m_historyWriteIndex + kHistoryRows - m_historyCount) % kHistoryRows;
        for (std::size_t index = 0; index < m_historyCount; ++index) {
            request->rows.push_back(m_history[(oldest + index) % kHistoryRows]);
        }
    }
    m_renderWorker->submit(std::move(request));
}

void WaterfallWidget::beginInteractiveRender()
{
    if (!m_snapshot || m_historyCount == 0 || !m_renderWorker) return;
    requestRender(true, false);
    if (m_renderSettleTimer) m_renderSettleTimer->start();
}

void WaterfallWidget::requestFullQualityRender()
{
    if (!m_snapshot || m_historyCount == 0 || !m_renderWorker) return;
    requestRender(false, false);
}

bool WaterfallWidget::canIncrementallyRender() const
{
    if (!m_snapshot || m_image.isNull() || m_renderedInteractivePreview ||
        m_renderedHistoryCount == 0 || m_appliedGeneration != m_renderGeneration ||
        m_renderedImageWidth != m_image.width() ||
        m_renderedImageHeight != m_image.height() ||
        m_renderedViewStartHz != m_viewStartHz ||
        m_renderedViewEndHz != m_viewEndHz) {
        return false;
    }
    return m_renderedFrameSequence != m_snapshot->frame.sequence;
}

void WaterfallWidget::resetHistory()
{
    for (auto& snapshot : m_history) snapshot.reset();
    m_historyWriteIndex = 0;
    m_historyCount = 0;
    m_historyFrameLength = 0;
    m_historyStartFrequencyHz = 0.0;
    m_historyBinWidthHz = 0.0;
}

void WaterfallWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(5, 12, 22));
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    ensureImage();
    const QRectF plot = plotRect();
    painter.drawImage(plot, m_image);
    painter.setPen(QColor(177, 197, 218));
    painter.drawText(QRectF(12, 4, width() - 24, 18),
                     Qt::AlignLeft, QStringLiteral("时频瀑布图"));
    painter.drawText(QRectF(10, plot.top(), 54, 18), Qt::AlignRight, QStringLiteral("1"));
    painter.drawText(QRectF(10, plot.center().y() - 9, 54, 18), Qt::AlignRight, QStringLiteral("50"));
    painter.drawText(QRectF(10, plot.bottom() - 18, 54, 18), Qt::AlignRight, QStringLiteral("100"));
    painter.setPen(QColor(128, 158, 187));
    painter.drawText(QRectF(0, 4, 65, 18), Qt::AlignRight, QStringLiteral("帧数"));

    if (m_hasSelection && m_viewInitialized) {
        const qreal selectedX = plot.left() + plot.width() *
            std::clamp((m_selectedFrequencyHz - m_viewStartHz) /
                       (m_viewEndHz - m_viewStartHz), 0.0, 1.0);
        painter.setPen(QPen(QColor(255, 232, 122), 1.2, Qt::DashLine));
        painter.drawLine(QPointF(selectedX, plot.top()), QPointF(selectedX, plot.bottom()));
        painter.setPen(QColor(255, 232, 122));
        painter.drawText(QRectF(std::min<qreal>(selectedX + 8, plot.right() - 210),
                                plot.top() + 8, 204, 18), Qt::AlignLeft,
                         QStringLiteral("选中 %1 MHz")
                             .arg(m_selectedFrequencyHz / 1e6, 0, 'f', 3));
    }

    if (m_zoomSelecting) {
        QRect selection(m_pressPosition, m_currentPosition);
        selection = selection.intersected(plot.toRect());
        painter.setPen(QPen(QColor(85, 205, 255), 1.0, Qt::DashLine));
        painter.setBrush(QColor(46, 151, 220, 42));
        painter.drawRect(selection);
    }
}

void WaterfallWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    ensureImage();
    beginInteractiveRender();
}

void WaterfallWidget::mousePressEvent(QMouseEvent* event)
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

void WaterfallWidget::mouseMoveEvent(QMouseEvent* event)
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

void WaterfallWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_leftPressed) {
        m_currentPosition = event->position().toPoint();
        if (m_zoomSelecting && plotRect().contains(m_pressPosition) && plotRect().contains(m_currentPosition)) {
            const double first = frequencyAt(m_pressPosition);
            const double second = frequencyAt(m_currentPosition);
            if (applyView(std::min(first, second), std::max(first, second), true)) {
                beginInteractiveRender();
            }
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

void WaterfallWidget::wheelEvent(QWheelEvent* event)
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
    if (applyView(anchor - newWidth * ratio,
                  anchor + newWidth * (1.0 - ratio), true)) {
        beginInteractiveRender();
    }
    event->accept();
}

void WaterfallWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        resetView();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

} // namespace scn::app
