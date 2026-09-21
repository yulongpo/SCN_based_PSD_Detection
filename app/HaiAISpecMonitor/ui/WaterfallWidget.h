#pragma once

#include "../../../algorithm/types/DisplayTypes.h"
#include "Direct2DChartRenderer.h"

#include <QImage>
#include <QPoint>
#include <QRectF>
#include <QTimer>
#include <QWidget>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

class QPaintEngine;

namespace scn::app
{

class WaterfallRenderWorker;

class WaterfallWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit WaterfallWidget(QWidget* parent = nullptr);
    ~WaterfallWidget() override;

public slots:
    void setSnapshot(const algorithm::DisplaySnapshotPtr& snapshot);
    void setDisplayDomain(double startHz, double endHz, double referenceLevelDbm);
    void setFrequencyView(double startHz, double endHz);
    void setSelectedFrequency(double frequencyHz);
    void resetView();
    void clear();

    // Called by the render worker through a queued UI-thread callback.
    void acceptRenderedImage(std::uint64_t generation,
                             std::uint64_t viewGeneration,
                             bool interactivePreview,
                             std::size_t historyCount,
                             std::uint64_t frameSequence,
                             double displayMinDb,
                             double displayMaxDb,
                             QImage image);

signals:
    void viewRangeChanged(double startHz, double endHz);
    void frequencySelected(double frequencyHz);

protected:
    QPaintEngine* paintEngine() const override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    bool applyView(double startHz, double endHz, bool notify);
    void ensureImage();
    QRectF plotRect() const;
    bool fullRange(double& startHz, double& endHz) const;
    double frequencyAt(const QPointF& position) const;
    void panByPixels(int deltaPixels);
    void selectAt(const QPointF& position);
    void showRestoreMenu(const QPoint& globalPosition);
    void requestRender(bool interactivePreview, bool incremental);
    void beginInteractiveRender();
    void requestFullQualityRender();
    bool canIncrementallyRender() const;
    void resetHistory();

    QImage m_image;
    std::array<QRgb, 256> m_palette{};
    algorithm::DisplaySnapshotPtr m_snapshot;
    std::unique_ptr<WaterfallRenderWorker> m_renderWorker;
    std::vector<algorithm::DisplaySnapshotPtr> m_history;
    std::size_t m_historyWriteIndex = 0;
    std::size_t m_historyCount = 0;
    std::size_t m_historyFrameLength = 0;
    double m_historyStartFrequencyHz = 0.0;
    double m_historyBinWidthHz = 0.0;
    std::uint64_t m_renderGeneration = 0;
    std::uint64_t m_appliedGeneration = 0;
    std::uint64_t m_viewGeneration = 0;
    double m_renderedViewStartHz = 0.0;
    double m_renderedViewEndHz = 0.0;
    int m_renderedImageWidth = 0;
    int m_renderedImageHeight = 0;
    std::size_t m_renderedHistoryCount = 0;
    std::uint64_t m_renderedFrameSequence = 0;
    double m_renderedDisplayMinDb = -120.0;
    double m_renderedDisplayMaxDb = 0.0;
    bool m_renderedInteractivePreview = false;
    QTimer* m_renderSettleTimer = nullptr;
    Direct2DChartRenderer m_direct2D;
    double m_viewStartHz = 0.0;
    double m_viewEndHz = 0.0;
    double m_displayStartHz = 0.0;
    double m_displayEndHz = 0.0;
    double m_displayMaxDb = 0.0;
    double m_displayMinDb = -100.0;
    bool m_hasDisplayDomain = false;
    double m_selectedFrequencyHz = 0.0;
    bool m_viewInitialized = false;
    bool m_manualView = false;
    bool m_hasSelection = false;
    bool m_leftPressed = false;
    bool m_zoomSelecting = false;
    bool m_middlePanning = false;
    QPoint m_pressPosition;
    QPoint m_currentPosition;
};

} // namespace scn::app
