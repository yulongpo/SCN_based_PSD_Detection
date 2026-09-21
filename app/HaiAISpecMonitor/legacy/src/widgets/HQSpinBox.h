#pragma once

#include <QTimer>
#include <QWidget>

class QLineEdit;
class QPropertyAnimation;

/**
 * @brief 与 HQComboBox/HQLineEdit 风格统一的自绘 SpinBox
 *
 * 支持 int/double 模式切换，右侧上下箭头自绘，长按自动递增/递减。
 */
class HQSpinBox : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal emphasisProgress READ emphasisProgress WRITE setEmphasisProgress)

public:
    explicit HQSpinBox(QWidget *parent = nullptr);
    ~HQSpinBox() override;

    double value() const { return m_value; }
    double minimum() const { return m_min; }
    double maximum() const { return m_max; }
    int decimals() const { return m_decimals; }
    double singleStep() const { return m_step; }

    /** 设置当前值（自动 clamp 到 [min, max]） */
    void setValue(double value);

    /** 设置取值范围 */
    void setRange(double min, double max);

    /**
     * @brief 设置小数位数
     * @param decimals 0 表示整数模式，>0 表示浮点模式
     */
    void setDecimals(int decimals);

    /** 设置单步增量 */
    void setSingleStep(double step);

    QSize sizeHint() const override;

signals:
    void valueChanged(double value);

protected:
    void enterEvent(QEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void applyThemeAppearance(bool night);
    void onAutoRepeat();

private:
    qreal emphasisProgress() const { return m_emphasisProgress; }
    void setEmphasisProgress(qreal progress);

    QColor themeColor(const QString &key, const QColor &fallback = QColor()) const;
    void animateEmphasisTo(qreal value);
    qreal targetEmphasis() const;
    void refreshMetrics();
    int scaledPx(int designPx, int min = 0) const;

    /** 将值格式化为显示文本 */
    QString valueToText() const;

    /** 根据鼠标位置判断点击区域：-1=上箭头，0=都不是，1=下箭头 */
    int arrowAtPos(const QPoint &pos) const;
    QRectF upArrowRect() const;
    QRectF downArrowRect() const;

    void stepBy(int direction);

    QLineEdit *m_lineEdit;                     // 内部输入框
    QPropertyAnimation *m_emphasisAnimation;   // 状态过渡动画
    QTimer *m_autoRepeatTimer;                 // 长按自动递增/递减定时器

    double m_value;                            // 当前值
    double m_min;                              // 最小值
    double m_max;                              // 最大值
    int m_decimals;                            // 小数位数（0=整数模式）
    double m_step;                             // 单步增量
    qreal m_emphasisProgress;                  // 当前动画进度
    bool m_hovered;                            // 鼠标悬停
    bool m_focused;                            // 内部输入框是否持有焦点
    int m_pressedArrow;                        // 当前按下的箭头方向（-1/0/1）
    bool m_cursorOverridden = false;           // 是否已设置全局禁止光标覆盖
};
