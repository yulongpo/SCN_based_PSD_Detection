#pragma once

#include <QPointF>
#include <QRectF>
#include <QString>
#include <QWidget>

class QEnterEvent;
class QMouseEvent;
class QPaintEvent;
class QResizeEvent;
class QTimer;
class QWheelEvent;

namespace scn::app
{

class FrequencyNavigatorWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit FrequencyNavigatorWidget(QWidget* parent = nullptr);

    void setDomain(double startHz, double endHz);
    void setViewRange(double startHz, double endHz);

signals:
    void viewRangeRequested(double startHz, double endHz);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    enum class Interaction
    {
        None,
        Pan,
        ResizeStart,
        ResizeEnd
    };

    QRectF trackRect() const;
    double frequencyAtX(double x) const;
    double xAtFrequency(double frequencyHz) const;
    bool normalizeRange(double& startHz, double& endHz) const;
    void requestUserRange(double startHz, double endHz, bool immediate);
    void flushPendingRequest();
    void updateCursor(const QPointF& position);

    double m_domainStartHz = 0.0;
    double m_domainEndHz = 0.0;
    double m_viewStartHz = 0.0;
    double m_viewEndHz = 0.0;
    double m_pressX = 0.0;
    double m_pressViewStartHz = 0.0;
    double m_pressViewEndHz = 0.0;
    double m_pendingStartHz = 0.0;
    double m_pendingEndHz = 0.0;
    bool m_hasDomain = false;
    bool m_hasView = false;
    bool m_hasPendingRequest = false;
    Interaction m_interaction = Interaction::None;
    QTimer* m_requestTimer = nullptr;
};

} // namespace scn::app
