#pragma once

#include <QtGlobal>
#include <QtMath>

// ============================================================
// DPR（Device Pixel Ratio）工具宏
//
// 用于将设计稿中的物理像素尺寸转换为当前屏幕下的逻辑像素，
// 保证在高分屏（Retina）上控件保持正确的物理大小。
// 设计思路：以 2x 屏为基准制作设计稿，代码中直接写设计稿数值，
// 通过除以 dpr 自动适配 1x / 1.5x / 2x / 2.5x / 3x 等不同缩放比。
// ============================================================

// DPR_INT: 物理像素 → 逻辑像素整数，带最小值保护。
// 适用于控件宽高、间距、边框宽度等需要整数结果的场景。
// 示例：
//   qMax(40, static_cast<int>(84.0 / m_dpr))   → DPR_INT(84, m_dpr, 40)
//   qMax(10, static_cast<int>(20.0 / dpr))      → DPR_INT(20, dpr, 10)
#define DPR_INT(val, dpr, min)  qMax((min), static_cast<int>((val) / (dpr)))

// DPR_REAL: 物理像素 → 逻辑像素浮点。
// 适用于圆角半径、画笔宽度、图标尺寸等需要浮点精度的场景。
// 示例：
//   32.0 / m_dpr    → DPR_REAL(32.0, m_dpr)
//   1.0 / m_dpr     → DPR_REAL(1.0, m_dpr)
//   0.5 / m_dpr     → DPR_REAL(0.5, m_dpr)
#define DPR_REAL(val, dpr)      ((val) / (dpr))

// LOGICAL_TO_PHYSICAL: 逻辑像素 → 物理像素整数（四舍五入）。
// 适用于 QPixmap 缓冲区尺寸、截图区域等需要物理像素整数的场景。
// 示例：
//   qRound(logicalW * dpr)   → LOGICAL_TO_PHYSICAL(logicalW, dpr)
#define LOGICAL_TO_PHYSICAL(val, dpr)  qRound((val) * (dpr))
