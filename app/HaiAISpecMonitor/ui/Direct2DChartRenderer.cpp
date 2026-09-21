#include "Direct2DChartRenderer.h"

#include <QByteArray>
#include <QDebug>

#ifdef Q_OS_WIN
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <dxgiformat.h>
#endif

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace scn::app
{

#ifdef Q_OS_WIN
namespace
{
template <typename T>
void releaseNative(T*& object)
{
    if (object) {
        object->Release();
        object = nullptr;
    }
}

D2D1_COLOR_F toD2DColor(const QColor& color)
{
    return D2D1::ColorF(color.redF(), color.greenF(), color.blueF(), color.alphaF());
}

D2D1_RECT_F toD2DRect(const QRectF& rect)
{
    return D2D1::RectF(static_cast<float>(rect.left()), static_cast<float>(rect.top()),
                       static_cast<float>(rect.right()), static_cast<float>(rect.bottom()));
}

D2D1_POINT_2F toD2DPoint(const QPointF& point)
{
    return D2D1::Point2F(static_cast<float>(point.x()), static_cast<float>(point.y()));
}
}
#endif

struct Direct2DChartRenderer::NativeState
{
#ifdef Q_OS_WIN
    ID2D1Factory* factory = nullptr;
    IDWriteFactory* writeFactory = nullptr;
    ID2D1HwndRenderTarget* target = nullptr;
    ID2D1Bitmap* bitmap = nullptr;
    HWND window = nullptr;
    QSize targetPixels;
    QSize logicalSize;
    float targetDpi = 96.0F;
    qreal devicePixelRatio = 1.0;
    qint64 bitmapKey = 0;
    QImage bitmapSource;
    bool inFrame = false;
#endif
};

Direct2DChartRenderer::Direct2DChartRenderer()
    : m_native(new NativeState)
{
}

Direct2DChartRenderer::~Direct2DChartRenderer()
{
    invalidate();
#ifdef Q_OS_WIN
    releaseNative(m_native->writeFactory);
    releaseNative(m_native->factory);
#endif
    delete m_native;
    m_native = nullptr;
}

bool Direct2DChartRenderer::begin(void* nativeWindowHandle, const QSize& logicalSize,
                                  qreal devicePixelRatio,
                                  const QColor& background)
{
#ifdef Q_OS_WIN
    if (!nativeWindowHandle || logicalSize.width() <= 0 || logicalSize.height() <= 0) return false;

    auto* hwnd = static_cast<HWND>(nativeWindowHandle);
    if (!m_native->factory) {
        const HRESULT factoryResult = D2D1CreateFactory(
            D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_native->factory);
        if (FAILED(factoryResult)) return false;
    }
    if (!m_native->writeFactory) {
        const HRESULT writeResult = DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(&m_native->writeFactory));
        if (FAILED(writeResult)) return false;
    }

    RECT clientRect{};
    if (!GetClientRect(hwnd, &clientRect)) return false;
    const QSize clientPixels(std::max<LONG>(0, clientRect.right - clientRect.left),
                             std::max<LONG>(0, clientRect.bottom - clientRect.top));
    if (clientPixels.width() <= 0 || clientPixels.height() <= 0) return false;

    const UINT windowDpi = GetDpiForWindow(hwnd);
    const float dpiFromQt = static_cast<float>(96.0 * std::max<qreal>(1.0, devicePixelRatio));
    const float targetDpi = windowDpi > 0 ? static_cast<float>(windowDpi) : dpiFromQt;

#ifndef NDEBUG
    const QSize expectedPixels(qRound(logicalSize.width() * std::max<qreal>(1.0, devicePixelRatio)),
                               qRound(logicalSize.height() * std::max<qreal>(1.0, devicePixelRatio)));
    if (std::abs(clientPixels.width() - expectedPixels.width()) > 2 ||
        std::abs(clientPixels.height() - expectedPixels.height()) > 2) {
        qWarning().nospace() << "Direct2D client/Qt size mismatch: logical=" << logicalSize
                             << ", clientPixels=" << clientPixels
                             << ", expectedPixels=" << expectedPixels
                             << ", dpr=" << devicePixelRatio
                             << ", dpi=" << targetDpi;
    }
#endif

    const bool targetChanged = !m_native->target || m_native->window != hwnd;
    const bool sizeChanged = m_native->targetPixels != clientPixels ||
        m_native->logicalSize != logicalSize ||
        std::abs(m_native->targetDpi - targetDpi) > 0.01F;
    if (targetChanged) {
        releaseNative(m_native->bitmap);
        releaseNative(m_native->target);
        m_native->bitmapKey = 0;
        m_native->bitmapSource = {};
        m_native->window = hwnd;
        const auto renderProperties = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_IGNORE));
        const auto hwndProperties = D2D1::HwndRenderTargetProperties(
            hwnd, D2D1::SizeU(static_cast<UINT32>(clientPixels.width()),
                              static_cast<UINT32>(clientPixels.height())));
        const HRESULT targetResult = m_native->factory->CreateHwndRenderTarget(
            renderProperties, hwndProperties, &m_native->target);
        if (FAILED(targetResult)) return false;
        m_native->targetPixels = clientPixels;
        m_native->logicalSize = logicalSize;
        m_native->targetDpi = targetDpi;
        m_native->devicePixelRatio = devicePixelRatio;
        m_native->target->SetDpi(targetDpi, targetDpi);
    } else if (sizeChanged) {
        const HRESULT resizeResult = m_native->target->Resize(
            D2D1::SizeU(static_cast<UINT32>(clientPixels.width()),
                        static_cast<UINT32>(clientPixels.height())));
        if (FAILED(resizeResult)) {
            invalidate();
            return false;
        }
        releaseNative(m_native->bitmap);
        m_native->bitmapKey = 0;
        m_native->bitmapSource = {};
        m_native->targetPixels = clientPixels;
        m_native->logicalSize = logicalSize;
        m_native->targetDpi = targetDpi;
        m_native->devicePixelRatio = devicePixelRatio;
        m_native->target->SetDpi(targetDpi, targetDpi);
    } else {
        m_native->logicalSize = logicalSize;
        m_native->devicePixelRatio = devicePixelRatio;
    }

    m_native->target->BeginDraw();
    m_native->target->Clear(toD2DColor(background));
    m_native->inFrame = true;
    return true;
#else
    Q_UNUSED(nativeWindowHandle)
    Q_UNUSED(logicalSize)
    Q_UNUSED(devicePixelRatio)
    Q_UNUSED(background)
    return false;
#endif
}

void Direct2DChartRenderer::end()
{
#ifdef Q_OS_WIN
    if (!m_native->target || !m_native->inFrame) return;
    const HRESULT result = m_native->target->EndDraw();
    m_native->inFrame = false;
    if (result == D2DERR_RECREATE_TARGET) invalidate();
#endif
}

void Direct2DChartRenderer::invalidate()
{
#ifdef Q_OS_WIN
    if (m_native->inFrame && m_native->target) {
        m_native->target->EndDraw();
        m_native->inFrame = false;
    }
    releaseNative(m_native->bitmap);
    releaseNative(m_native->target);
    m_native->window = nullptr;
    m_native->targetPixels = {};
    m_native->logicalSize = {};
    m_native->targetDpi = 96.0F;
    m_native->devicePixelRatio = 1.0;
    m_native->bitmapKey = 0;
    m_native->bitmapSource = {};
#endif
}

void Direct2DChartRenderer::pushClip(const QRectF& rect)
{
#ifdef Q_OS_WIN
    if (!m_native->target || !m_native->inFrame) return;
    m_native->target->PushAxisAlignedClip(toD2DRect(rect), D2D1_ANTIALIAS_MODE_ALIASED);
#else
    Q_UNUSED(rect)
#endif
}

void Direct2DChartRenderer::popClip()
{
#ifdef Q_OS_WIN
    if (!m_native->target || !m_native->inFrame) return;
    m_native->target->PopAxisAlignedClip();
#endif
}

void Direct2DChartRenderer::fillRect(const QRectF& rect, const QColor& color)
{
#ifdef Q_OS_WIN
    if (!m_native->target || !m_native->inFrame) return;
    ID2D1SolidColorBrush* brush = nullptr;
    if (SUCCEEDED(m_native->target->CreateSolidColorBrush(toD2DColor(color), &brush))) {
        m_native->target->FillRectangle(toD2DRect(rect), brush);
    }
    releaseNative(brush);
#else
    Q_UNUSED(rect)
    Q_UNUSED(color)
#endif
}

void Direct2DChartRenderer::drawRect(const QRectF& rect, const QColor& color, float width)
{
#ifdef Q_OS_WIN
    if (!m_native->target || !m_native->inFrame) return;
    ID2D1SolidColorBrush* brush = nullptr;
    if (SUCCEEDED(m_native->target->CreateSolidColorBrush(toD2DColor(color), &brush))) {
        m_native->target->DrawRectangle(toD2DRect(rect), brush, width);
    }
    releaseNative(brush);
#else
    Q_UNUSED(rect)
    Q_UNUSED(color)
    Q_UNUSED(width)
#endif
}

void Direct2DChartRenderer::drawLine(const QPointF& first, const QPointF& second,
                                     const QColor& color, float width)
{
#ifdef Q_OS_WIN
    if (!m_native->target || !m_native->inFrame) return;
    ID2D1SolidColorBrush* brush = nullptr;
    if (SUCCEEDED(m_native->target->CreateSolidColorBrush(toD2DColor(color), &brush))) {
        m_native->target->DrawLine(toD2DPoint(first), toD2DPoint(second), brush, width);
    }
    releaseNative(brush);
#else
    Q_UNUSED(first)
    Q_UNUSED(second)
    Q_UNUSED(color)
    Q_UNUSED(width)
#endif
}

void Direct2DChartRenderer::drawPolyline(const QPolygonF& points, const QPointF& offset,
                                          const QColor& color, float width)
{
#ifdef Q_OS_WIN
    if (!m_native->target || !m_native->inFrame || points.size() < 2) return;
    ID2D1SolidColorBrush* brush = nullptr;
    if (FAILED(m_native->target->CreateSolidColorBrush(toD2DColor(color), &brush))) return;

    ID2D1PathGeometry* geometry = nullptr;
    ID2D1GeometrySink* sink = nullptr;
    if (SUCCEEDED(m_native->factory->CreatePathGeometry(&geometry)) &&
        SUCCEEDED(geometry->Open(&sink))) {
        sink->BeginFigure(toD2DPoint(points.first() + offset), D2D1_FIGURE_BEGIN_HOLLOW);
        for (int index = 1; index < points.size(); ++index) {
            sink->AddLine(toD2DPoint(points.at(index) + offset));
        }
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        const HRESULT closeResult = sink->Close();
        releaseNative(sink);
        if (SUCCEEDED(closeResult)) {
            m_native->target->DrawGeometry(geometry, brush, width);
        }
    }
    releaseNative(sink);
    releaseNative(geometry);
    releaseNative(brush);
#else
    Q_UNUSED(points)
    Q_UNUSED(offset)
    Q_UNUSED(color)
    Q_UNUSED(width)
#endif
}

void Direct2DChartRenderer::drawEllipse(const QPointF& center, float radius,
                                        const QColor& color, bool filled)
{
#ifdef Q_OS_WIN
    if (!m_native->target || !m_native->inFrame) return;
    ID2D1SolidColorBrush* brush = nullptr;
    if (SUCCEEDED(m_native->target->CreateSolidColorBrush(toD2DColor(color), &brush))) {
        const auto ellipse = D2D1::Ellipse(toD2DPoint(center), radius, radius);
        if (filled) m_native->target->FillEllipse(ellipse, brush);
        else m_native->target->DrawEllipse(ellipse, brush, 1.0F);
    }
    releaseNative(brush);
#else
    Q_UNUSED(center)
    Q_UNUSED(radius)
    Q_UNUSED(color)
    Q_UNUSED(filled)
#endif
}

void Direct2DChartRenderer::drawImage(const QImage& image, const QRectF& destination)
{
#ifdef Q_OS_WIN
    if (!m_native->target || !m_native->inFrame || image.isNull()) return;
    const QImage converted = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const UINT maximumBitmapSize = m_native->target->GetMaximumBitmapSize();
    const UINT maximumInt = static_cast<UINT>((std::numeric_limits<int>::max)());
    const int maximumTileSize = static_cast<int>(
        maximumBitmapSize < maximumInt ? maximumBitmapSize : maximumInt);
    if (maximumTileSize <= 0) return;

    const auto properties = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                          D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0F, 96.0F);

    // The waterfall intentionally renders up to plotWidth * 10 columns.
    // Upload it in tiles when the source is wider than the device bitmap
    // limit. Each tile is mapped to its exact destination fraction, so no
    // frequency columns are discarded or stretched at the seam.
    if (converted.width() > maximumTileSize || converted.height() > maximumTileSize) {
        releaseNative(m_native->bitmap);
        m_native->bitmapKey = 0;
        m_native->bitmapSource = {};
        for (int top = 0; top < converted.height(); top += maximumTileSize) {
            const int tileHeight = maximumTileSize < converted.height() - top
                ? maximumTileSize : converted.height() - top;
            for (int left = 0; left < converted.width(); left += maximumTileSize) {
                const int tileWidth = maximumTileSize < converted.width() - left
                    ? maximumTileSize : converted.width() - left;
                ID2D1Bitmap* tile = nullptr;
                const auto* source = converted.constScanLine(top) +
                    static_cast<std::ptrdiff_t>(left) * 4;
                const HRESULT result = m_native->target->CreateBitmap(
                    D2D1::SizeU(static_cast<UINT32>(tileWidth),
                                static_cast<UINT32>(tileHeight)),
                    source, static_cast<UINT32>(converted.bytesPerLine()),
                    properties, &tile);
                if (FAILED(result) || !tile) {
                    releaseNative(tile);
                    continue;
                }
                const qreal leftRatio = static_cast<qreal>(left) / converted.width();
                const qreal rightRatio = static_cast<qreal>(left + tileWidth) / converted.width();
                const qreal topRatio = static_cast<qreal>(top) / converted.height();
                const qreal bottomRatio = static_cast<qreal>(top + tileHeight) / converted.height();
                const QRectF tileDestination(
                    destination.left() + destination.width() * leftRatio,
                    destination.top() + destination.height() * topRatio,
                    destination.width() * (rightRatio - leftRatio),
                    destination.height() * (bottomRatio - topRatio));
                m_native->target->DrawBitmap(tile, toD2DRect(tileDestination), 1.0F,
                                             D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
                releaseNative(tile);
            }
        }
        return;
    }

    const qint64 key = image.cacheKey();
    if (!m_native->bitmap || m_native->bitmapKey != key) {
        releaseNative(m_native->bitmap);
        const HRESULT result = m_native->target->CreateBitmap(
            D2D1::SizeU(static_cast<UINT32>(converted.width()),
                        static_cast<UINT32>(converted.height())),
            converted.constBits(), static_cast<UINT32>(converted.bytesPerLine()),
            properties, &m_native->bitmap);
        if (FAILED(result)) return;
        m_native->bitmapKey = key;
        m_native->bitmapSource = converted;
    }
    m_native->target->DrawBitmap(m_native->bitmap, toD2DRect(destination), 1.0F,
                                 D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
#else
    Q_UNUSED(image)
    Q_UNUSED(destination)
#endif
}

void Direct2DChartRenderer::drawText(const QString& text, const QRectF& rect,
                                     const QColor& color, float fontSize,
                                     Qt::Alignment alignment)
{
#ifdef Q_OS_WIN
    if (!m_native->target || !m_native->inFrame || !m_native->writeFactory || text.isEmpty()) return;
    IDWriteTextFormat* format = nullptr;
    const HRESULT formatResult = m_native->writeFactory->CreateTextFormat(
        L"Microsoft YaHei UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, fontSize,
        L"zh-CN", &format);
    if (FAILED(formatResult)) return;
    format->SetTextAlignment(alignment.testFlag(Qt::AlignRight)
        ? DWRITE_TEXT_ALIGNMENT_TRAILING
        : alignment.testFlag(Qt::AlignHCenter)
            ? DWRITE_TEXT_ALIGNMENT_CENTER : DWRITE_TEXT_ALIGNMENT_LEADING);
    format->SetParagraphAlignment(alignment.testFlag(Qt::AlignVCenter)
        ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER
        : alignment.testFlag(Qt::AlignBottom)
            ? DWRITE_PARAGRAPH_ALIGNMENT_FAR : DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

    ID2D1SolidColorBrush* brush = nullptr;
    if (SUCCEEDED(m_native->target->CreateSolidColorBrush(toD2DColor(color), &brush))) {
        const auto* characters = reinterpret_cast<const WCHAR*>(text.utf16());
        m_native->target->DrawText(characters, static_cast<UINT32>(text.size()), format,
                                   toD2DRect(rect), brush, D2D1_DRAW_TEXT_OPTIONS_NONE,
                                   DWRITE_MEASURING_MODE_NATURAL);
    }
    releaseNative(brush);
    releaseNative(format);
#else
    Q_UNUSED(text)
    Q_UNUSED(rect)
    Q_UNUSED(color)
    Q_UNUSED(fontSize)
    Q_UNUSED(alignment)
#endif
}

} // namespace scn::app
