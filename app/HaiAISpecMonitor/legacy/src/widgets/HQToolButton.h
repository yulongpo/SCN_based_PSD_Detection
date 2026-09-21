#pragma once

#include <QPushButton>
#include <QIcon>
#include <QColor>

class QPainter;

/**
 * @brief 通用工具栏按钮控件
 *
 * 支持图标 + 文字、自定义边框色/悬停色/圆角。
 * 通过 QPainter 自绘，避免高 DPI 下 QSS border 模糊的问题，
 * 适用于底部操作栏中的导出、清理等图标文字按钮。
 *
 * 用法示例：
 * @code
 * auto *btn = new HQToolButton(this);
 * btn->setHQIcon(QIcon(":/export.png"), 12, 12);
 * btn->setText(QStringLiteral("导出 "));
 * btn->setFixedSize(79, 28);
 * @endcode
 */
class HQToolButton : public QPushButton
{
    Q_OBJECT

public:
    /**
     * @brief 构造工具栏按钮
     * @param parent 父控件
     */
    explicit HQToolButton(QWidget *parent = nullptr);

    /**
     * @brief 设置图标及图标尺寸
     * @param icon   图标
     * @param iconW  图标宽度（逻辑像素，DPR 自动缩放）
     * @param iconH  图标高度（逻辑像素，DPR 自动缩放）
     */
    void setHQIcon(const QIcon &icon, int iconW = 12, int iconH = 12);

    /**
     * @brief 设置圆角半径
     * @param logicalRadius 逻辑像素半径（DPR 自动缩放）
     */
    void setHQRadius(int logicalRadius);

    /**
     * @brief 设置边框颜色
     * @param color 边框颜色，默认 rgba(255,255,255,51)
     */
    void setBorderColor(const QColor &color);

    /**
     * @brief 设置悬停背景色
     * @param color 悬停时填充背景色，默认 rgba(255,255,255,25)
     */
    void setHoverBackground(const QColor &color);

    /**
     * @brief 设置文字颜色
     * @param color 文字颜色，默认使用 palette().buttonText()
     */
    void setTextColor(const QColor &color);

protected:
    /** @brief 自绘：背景 → 图标+文字 → 边框 */
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QIcon  m_icon;                          ///< 按钮图标
    QSize  m_iconSize      = QSize(12, 12); ///< 图标尺寸（逻辑像素）
    int    m_logicalRadius = 6;             ///< 圆角半径（逻辑像素）
    QColor m_borderColor   = QColor(255, 255, 255, 51);  ///< 边框色
    QColor m_hoverBg       = QColor(255, 255, 255, 25);  ///< 悬停背景
    QColor m_textColor;                     ///< 文字色（无效则用 palette）
    bool   m_hovered       = false;         ///< 鼠标悬停标志
};
