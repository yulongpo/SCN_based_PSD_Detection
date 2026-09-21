#pragma once

#include "../../../algorithm/types/DisplayTypes.h"
#include "Direct2DChartRenderer.h"
#include "SpectrumRenderWorker.h"

#include <QPoint>
#include <QPolygonF>
#include <QRectF>
#include <QWidget>

#include <cstdint>

class QThread;
class QTimer;
class QVariantAnimation;
class QFrame;
class QPushButton;
class QPaintEngine;

namespace scn::app
{

class SpectrumWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit SpectrumWidget(QWidget* parent = nullptr);
    ~SpectrumWidget() override;

public slots:
    void setSnapshot(const algorithm::DisplaySnapshotPtr& snapshot);
    void setDisplayDomain(double startHz, double endHz, double referenceLevelDbm);
    void setFrequencyView(double startHz, double endHz);
    void setSelectedFrequency(double frequencyHz);
    void resetView();
    void clear();

public:
    bool hasFrequencyView() const noexcept;
    double viewStartFrequency() const noexcept;
    double viewEndFrequency() const noexcept;
    bool hasSelectedFrequency() const noexcept;
    double selectedFrequency() const noexcept;
    bool realtimeSpectrumVisible() const noexcept;
    bool maxSpectrumVisible() const noexcept;
    bool averageSpectrumVisible() const noexcept;
    bool detectionMarkersVisible() const noexcept;

    void setRealtimeSpectrumVisible(bool visible);
    void setMaxSpectrumVisible(bool visible);
    void setAverageSpectrumVisible(bool visible);
    void setDetectionMarkersVisible(bool visible);

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
    QRectF plotRect() const;
    bool fullRange(double& startHz, double& endHz) const;
    double frequencyAt(const QPointF& position) const;
    std::size_t indexAtFrequency(double frequencyHz) const;
    void applyView(double startHz, double endHz, bool notify);
    void panByPixels(int deltaPixels);
    void panVerticalByPixels(int deltaPixels);
    void zoomVerticalAt(const QPointF& position, double steps);
    bool isXAxisArea(const QPointF& position) const;
    bool isYAxisArea(const QPointF& position) const;
    void updateInteractionCursor(const QPointF& position);
    void selectAt(const QPointF& position);
    void showRestoreMenu(const QPoint& globalPosition);
    void animateViewTo(double startHz, double endHz, bool manualViewAfterAnimation = true);
    void stopViewAnimation();
    void buildTraceToolbar();
    void positionTraceToolbar();
    void submitRenderRequest(bool interactivePreview);
    void requestFullRender();
    void acceptRenderResult(const SpectrumRenderResult& result);
    void drawPolylinePair(Direct2DChartRenderer& renderer, const QPolygonF& upper,
                          const QPolygonF& lower, const QColor& color) const;
    void drawDetectionMarkers(Direct2DChartRenderer& renderer, const QRectF& plot,
                              double viewStartHz, double viewEndHz) const;

    algorithm::DisplaySnapshotPtr m_snapshot;
    double m_viewStartHz = 0.0;
    double m_viewEndHz = 0.0;
    double m_selectedFrequencyHz = 0.0;
    float m_selectedPowerDb = 0.0F;
    bool m_viewInitialized = false;
    bool m_manualView = false;
    bool m_hasSelection = false;
    bool m_leftPressed = false;
    bool m_xAxisPanning = false;
    bool m_yAxisPanning = false;
    bool m_zoomSelecting = false;
    bool m_middlePanning = false;
    QPoint m_pressPosition;
    QPoint m_currentPosition;
    QVariantAnimation* m_viewAnimation = nullptr;
    QFrame* m_traceToolbar = nullptr;
    QPushButton* m_realtimeSpectrumButton = nullptr;
    QPushButton* m_maxSpectrumButton = nullptr;
    QPushButton* m_averageSpectrumButton = nullptr;
    QPushButton* m_detectionMarkersButton = nullptr;
    QThread* m_renderThread = nullptr;
    SpectrumRenderWorker* m_renderWorker = nullptr;
    QTimer* m_renderSettleTimer = nullptr;
    std::uint64_t m_renderGeneration = 0;
    std::uint64_t m_nextRenderRequestId = 0;
    std::uint64_t m_latestRenderRequestId = 0;
    std::uint64_t m_renderedRequestId = 0;
    std::uint64_t m_renderedFrameSequence = 0;
    double m_renderedViewStartHz = 0.0;
    double m_renderedViewEndHz = 0.0;
    double m_renderedDisplayMinDb = -120.0;
    double m_renderedDisplayMaxDb = 0.0;
    int m_renderedPlotWidth = 0;
    int m_renderedPlotHeight = 0;
    QPolygonF m_currentUpper;
    QPolygonF m_currentLower;
    QPolygonF m_maxUpper;
    QPolygonF m_maxLower;
    QPolygonF m_averageUpper;
    QPolygonF m_averageLower;
    double m_displayMinDb = -120.0;
    double m_displayMaxDb = 0.0;
    double m_displayStartHz = 0.0;
    double m_displayEndHz = 0.0;
    double m_viewMinDb = -120.0;
    double m_viewMaxDb = 0.0;
    bool m_verticalViewInitialized = false;
    bool m_hasDisplayDomain = false;
    bool m_showRealtimeSpectrum = true;
    bool m_showMaxSpectrum = false;
    bool m_showAverageSpectrum = false;
    bool m_showDetectionMarkers = true;
    Direct2DChartRenderer m_direct2D;
};

} // namespace scn::app
