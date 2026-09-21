#pragma once

#include <QWidget>

class QPainter;

/**
 * @brief 自绘复选框控件（支持右侧文字）
 *
 * 圆角 4px，边框 rgb(115,115,120)，选中蓝底(#0A8CFE)+白色对勾。
 * 可通过 setText 在复选框右侧显示文字，适用于"仅显示告警"等场景。
 * 提供静态 draw/renderPixmap 方法供外部复用。
 */
class HQCheckBox : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief 构造复选框
     * @param parent 父控件
     */
    explicit HQCheckBox(QWidget *parent = nullptr);

    /** @brief 是否选中 */
    bool isChecked() const { return m_checked; }

    /**
     * @brief 设置选中状态
     * @param checked 是否选中
     */
    void setChecked(bool checked);

    /** @brief 获取复选框右侧文字 */
    QString text() const { return m_text; }
    /**
     * @brief 设置复选框右侧文字
     * @param text 显示文字
     */
    void setText(const QString &text);

    /**
     * @brief 设置文字颜色
     * @param color 文字颜色
     */
    void setTextColor(const QColor &color);

    /**
     * @brief 设置复选框最小像素尺寸（DPR_INT的min参数）
     *        默认为16，设为0则完全按DPR缩放不做下限约束
     * @param minSize 最小像素尺寸
     */
    void setMinBoxSize(int minSize);

    QSize sizeHint() const override;

    /**
     * @brief 在指定矩形内绘制复选框（供表格代理等外部复用）
     * @param painter QPainter 对象
     * @param rect    绘制区域（复选框会在该区域内居中）
     * @param checked 是否选中
     */
    static void draw(QPainter *painter, const QRectF &rect, bool checked);

    /**
     * @brief 渲染复选框图片
     * @param checked      是否选中
     * @param logicalSize  逻辑像素尺寸（默认 16px，会自动 DPR 缩放）
     * @return 复选框 QPixmap
     */
    static QPixmap renderPixmap(bool checked, int logicalSize = 16);

signals:
    /** @brief 状态变化 @param state Qt::Checked / Qt::Unchecked */
    void stateChanged(int state);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    bool    m_checked    = false;
    QString m_text;
    QColor  m_textColor  = QColor(255, 255, 255, 179);
    int     m_boxSize    = 16;
    int     m_spacing    = 6;
    int     m_minBoxSize = 16;  // DPR_INT 的 min 参数，默认 16
};
