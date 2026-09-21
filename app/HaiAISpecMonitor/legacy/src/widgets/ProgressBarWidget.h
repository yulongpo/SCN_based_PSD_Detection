#pragma once

#include <QWidget>
#include <QPixmap>
#include <QFont>
#include <QColor>
#include <QVariantAnimation>

/**
 * @brief 通用进度条控件
 *
 * 布局：[icon] [label] [=======progress bar=======] [100%]
 *
 * 支持功能：
 *   - 左侧图标 + 文字标签
 *   - 自定义进度条高度、背景色、填充色
 *   - 右侧百分比文字使用填充色（"填充色同步给右侧进度文字"）
 *   - 进度值改变时自动播放平滑动画（增/减）
 *   - DPR 缩放自适应
 */
class ProgressBarWidget : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(int progress READ value WRITE setValue)

public:
    explicit ProgressBarWidget(QWidget *parent = nullptr);

    /// 设置左侧图标（QPixmap，自动缩放至 iconSize）
    void setIcon(const QPixmap &pixmap, int iconSize = 16);

    /// 设置文字标签
    void setLabel(const QString &text);

    /// 设置进度值（0-100），自动触发平滑动画
    void setValue(int percent);

    /// 设置字体
    void setBarFont(const QFont &font);

    /// 设置进度条高度（逻辑像素，默认 4）
    void setBarHeight(int heightPx);

    /// 设置进度条背景色
    void setBarBgColor(const QColor &color);

    /// 设置进度条填充色（同时作用于右侧百分比文字）
    void setBarFillColor(const QColor &color);

    /// 设置标签文字颜色
    void setTextColor(const QColor &color);

    /// 当前进度值
    int value() const { return m_value; }

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QPixmap              m_icon;
    QString              m_label;
    int                  m_value       = 0;       ///< 当前显示值（动画插值中）
    int                  m_targetValue = 0;       ///< 目标值
    int                  m_iconSize    = 16;      ///< 图标尺寸（逻辑像素）
    int                  m_barHeight   = 4;       ///< 进度条高度（逻辑像素）
    QColor               m_barBgColor  = QColor(60, 60, 70);
    QColor               m_barFillColor = QColor(10, 140, 254);
    QColor               m_textColor   = QColor(200, 200, 200);
    QFont                m_font;
    QVariantAnimation   *m_animation   = nullptr;
};
