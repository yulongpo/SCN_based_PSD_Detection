#pragma once

#include <QWidget>
#include <QColor>

class QPainter;

/**
 * @brief 自定义 HSV 取色板控件
 *
 * 固定尺寸 132x159 的紧凑取色控件，包含：
 * - 上方二维色域选取区（X 轴饱和度递增，Y 轴明度递减）
 * - 下方色相拖动条
 * - 下方透明度拖动条（棋盘格背景）
 */
class HQColorWidget : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief 构造取色板控件
     * @param parent 父控件
     */
    explicit HQColorWidget(QWidget *parent = nullptr);

    /**
     * @brief 获取当前选中的颜色
     * @return 当前颜色（含 alpha 通道）
     */
    QColor currentColor() const;

    /**
     * @brief 设置当前颜色
     * @param color 目标颜色
     */
    void setCurrentColor(const QColor &color);

signals:
    /**
     * @brief 颜色实时改变（拖拽过程中持续发射）
     * @param color 当前颜色
     */
    void colorChanged(const QColor &color);

    /**
     * @brief 取色完成（鼠标释放时发射）
     * @param color 最终选定的颜色
     */
    void colorSelected(const QColor &color);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    /**
     * @brief 更新色域选取区的缓存像素图
     */
    void updatePickerPixmap();

    /**
     * @brief 绘制滑块把手
     * @param p 画笔
     * @param pos 把手中心坐标
     */
    void drawKnob(QPainter &p, const QPoint &pos) const;

    /**
     * @brief 绘制色相滑块
     * @param p 画笔
     */
    void drawHueSlider(QPainter &p);

    /**
     * @brief 绘制透明度滑块
     * @param p 画笔
     */
    void drawAlphaSlider(QPainter &p);

    /**
     * @brief 计算色域区指示器位置
     * @param color 目标颜色
     * @return 相对于本控件的坐标
     */
    QPoint indicatorPos(const QColor &color) const;

private:
    static constexpr int kWidgetWidth   = 132;
    static constexpr int kWidgetHeight  = 159;
    static constexpr int kPadding       = 6;
    static constexpr int kSliderHeight  = 16;
    static constexpr int kSliderGap     = 3;
    static constexpr int kKnobRadius    = 5;

    QRect   m_pickerRect;       ///< 色域选取区矩形
    QPixmap m_pickerPixmap;     ///< 色域渐变缓存像素图

    QRect   m_hueRect;          ///< 色相滑块矩形
    QRect   m_alphaRect;        ///< 透明度滑块矩形

    int m_hue   = 0;            ///< 色相 0-359
    int m_sat   = 255;          ///< 饱和度 0-255
    int m_val   = 255;          ///< 明度 0-255
    int m_alpha = 255;          ///< 透明度 0-255

    // 拖拽状态
    bool m_draggingPicker = false;
    bool m_draggingHue    = false;
    bool m_draggingAlpha  = false;
};
