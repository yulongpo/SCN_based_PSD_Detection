#pragma once

#include <QSplitter>
#include <QSplitterHandle>

class QPaintEvent;
class QVariantAnimation;

/**
 * @brief 频谱图与时频图之间的垂直分裂器（自定义分隔线样式）
 *
 * 分隔线样式要求：
 * - 默认：1px 灰色细线（颜色取主题 CollMonitor.borderColor）
 * - 悬停/拖动：3px 亮蓝色粗线（主题蓝 rgb(10,140,254)）
 * - 线宽变化仅影响绘制，handle 布局宽度恒定，
 *   因此分隔线变粗不会改变两侧 widget（频谱图/时频图）的尺寸
 */
class HQSplitter : public QSplitter
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数（固定垂直方向）
     * @param parent 父控件
     *
     * 自动设置 handle 宽度（按屏幕缩放换算，保证有足够的悬停/拖动空间），
     * 并禁止子控件被折叠为 0 高度。
     */
    explicit HQSplitter(QWidget *parent = nullptr);

protected:
    /**
     * @brief 创建自定义手柄（返回 HQSplitterHandle）
     * @return 自定义手柄实例
     */
    QSplitterHandle *createHandle() override;
};

/**
 * @brief 分裂器手柄（垂直分裂器上表现为一个水平条）
 *
 * 居中绘制一根分隔线：普通态为灰色细线，鼠标悬停或拖动时变为亮蓝粗线。
 */
class HQSplitterHandle : public QSplitterHandle
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param orientation 分裂器方向（决定手柄的条状走向）
     * @param parent      所属 HQSplitter
     */
    explicit HQSplitterHandle(Qt::Orientation orientation, QSplitter *parent);

protected:
    /** @brief 自绘分隔线（灰细线 / 蓝粗线），仅绘制变化不影响布局 */
    void paintEvent(QPaintEvent *event) override;

    /** @brief 处理悬停进入/离开事件，启动高亮过渡动画 */
    bool event(QEvent *event) override;

    /** @brief 按下拖动时启动高亮过渡动画（到蓝色粗线） */
    void mousePressEvent(QMouseEvent *event) override;

    /** @brief 释放后启动回退过渡动画（若鼠标仍悬停则保持高亮） */
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    /**
     * @brief 启动高亮过渡动画，从当前进度平滑过渡到目标进度
     * @param target 目标进度（0.0=常态灰色细线，1.0=悬停蓝色粗线）
     *
     * 始终以当前进度为起点，动画中途打断时不会跳变。
     */
    void animateTo(qreal target);

private:
    bool m_hovered = false;      ///< 鼠标是否悬停于手柄上
    bool m_pressed = false;      ///< 是否正在拖动分隔条
    qreal m_animProgress = 0.0;  ///< 高亮过渡动画进度（0.0~1.0，驱动颜色与线宽插值）
    QVariantAnimation *m_anim = nullptr;  ///< 高亮过渡动画（复用，避免频繁 new/delete）
};