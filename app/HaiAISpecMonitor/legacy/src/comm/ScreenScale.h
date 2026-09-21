#pragma once

#include <QtGlobal>

class QScreen;

/**
 * @brief 屏幕分辨率缩放管理器（单例）
 *
 * 以 1080p 分辨率为基准，按屏幕高度等比缩放所有控件尺寸。
 * geometry() 返回的是逻辑像素，需乘以 dpr 得到物理像素。
 *
 * 使用示例：
 *   const auto &ss = ScreenScale::instance();
 *   const int titleH = DPR_INT(50.0 * ss.scale(), ss.dpr(), 20);
 */
class ScreenScale
{
public:
    /// 返回全局唯一实例
    static ScreenScale &instance();

    /// 设备像素比（Device Pixel Ratio）
    qreal dpr() const { return m_dpr; }

    /// 屏幕物理高度（px），以 1440 为保底
    int screenHeight() const { return m_screenH; }

    /// 相对于 1440p 的缩放系数
    double scale() const { return m_scale; }

private:
    ScreenScale();
    ~ScreenScale() = default;

    // 禁止拷贝
    ScreenScale(const ScreenScale &) = delete;
    ScreenScale &operator=(const ScreenScale &) = delete;

    qreal  m_dpr;
    int    m_screenH;
    double m_scale;
};
