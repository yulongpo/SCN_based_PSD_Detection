#pragma once

#include <QColor>
#include <QImage>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QSize>
#include <QString>

namespace scn::app
{

// Windows-native chart surface used by the spectrum and waterfall widgets.
// The QWidget remains responsible for input/layout; Direct2D owns the chart
// pixels and avoids routing large trace/image paints through QPainter.
class Direct2DChartRenderer final
{
public:
    Direct2DChartRenderer();
    ~Direct2DChartRenderer();

    Direct2DChartRenderer(const Direct2DChartRenderer&) = delete;
    Direct2DChartRenderer& operator=(const Direct2DChartRenderer&) = delete;

    bool begin(void* nativeWindowHandle, const QSize& logicalSize,
               qreal devicePixelRatio, const QColor& background);
    void end();
    void invalidate();

    void pushClip(const QRectF& rect);
    void popClip();

    void fillRect(const QRectF& rect, const QColor& color);
    void drawRect(const QRectF& rect, const QColor& color, float width = 1.0F);
    void drawLine(const QPointF& first, const QPointF& second,
                  const QColor& color, float width = 1.0F);
    void drawPolyline(const QPolygonF& points, const QPointF& offset,
                      const QColor& color, float width = 1.0F);
    void drawEllipse(const QPointF& center, float radius,
                     const QColor& color, bool filled);
    void drawImage(const QImage& image, const QRectF& destination);
    void drawText(const QString& text, const QRectF& rect, const QColor& color,
                  float fontSize, Qt::Alignment alignment = Qt::AlignLeft);

private:
    struct NativeState;
    NativeState* m_native = nullptr;
};

} // namespace scn::app
