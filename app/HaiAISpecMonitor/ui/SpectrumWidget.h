#pragma once

#include "../../../algorithm/types/DisplayTypes.h"

#include <QPoint>
#include <QRectF>
#include <QWidget>

namespace scn::app
{

class SpectrumWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit SpectrumWidget(QWidget* parent = nullptr);

public slots:
    void setSnapshot(const algorithm::DisplaySnapshotPtr& snapshot);
    void setFrequencyView(double startHz, double endHz);
    void setSelectedFrequency(double frequencyHz);
    void resetView();
    void clear();

signals:
    void viewRangeChanged(double startHz, double endHz);
    void frequencySelected(double frequencyHz);

protected:
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
    void selectAt(const QPointF& position);
    void showRestoreMenu(const QPoint& globalPosition);

    algorithm::DisplaySnapshotPtr m_snapshot;
    double m_viewStartHz = 0.0;
    double m_viewEndHz = 0.0;
    double m_selectedFrequencyHz = 0.0;
    float m_selectedPowerDb = 0.0F;
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
