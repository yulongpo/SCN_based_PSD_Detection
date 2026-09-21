#include "ScreenScale.h"
#include <QGuiApplication>
#include <QScreen>

ScreenScale &ScreenScale::instance()
{
    static ScreenScale s;
    return s;
}

ScreenScale::ScreenScale()
    : m_dpr(1.0)
    , m_screenH(1440)
    , m_scale(1.0)
{
    // 以 1080p 分辨率为基准，按屏幕高度等比缩放所有控件尺寸.
    // 注意：geometry() 返回的是逻辑像素，需乘以 dpr 得到物理像素.
    const auto screen = QGuiApplication::primaryScreen();
    m_dpr = screen ? screen->devicePixelRatio() : 1.0;
    m_screenH = screen ? static_cast<int>(screen->geometry().height() * m_dpr) : 1080;
    m_scale = static_cast<double>(m_screenH) / 1080.0;
}
