#pragma once

#include <QWidget>

class QLineEdit;
class QPropertyAnimation;

/**
 * @brief 与 HQComboBox 风格统一的输入框组件，支持右侧单位文字
 *
 * 提供以下能力：
 * - 右侧可配置单位文字（如 "MHz"、"dBm" 等），通过 setUnit() 设置
 * - 完全自绘外观，不依赖 QSS 边框
 */
class HQFreqLineEdit : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal emphasisProgress READ emphasisProgress WRITE setEmphasisProgress)

public:
    explicit HQFreqLineEdit(QWidget *parent = nullptr);
    ~HQFreqLineEdit() override;

    /** 获取/设置输入框文本 */
    QString text() const;
    void setText(const QString &text);

    /**
     * @brief 获取/设置以 Hz 为单位的值
     *
     * value()   —— 解析当前文本（含单位），返回转换后的 Hz 数值
     * setValue()—— 以 Hz 为单位设置值，自动选择合适的单位（Hz/kHz/MHz/GHz）显示
     */
    int64_t value() const;
    void setValue(int64_t hz);

    /** 获取/设置占位提示文字 */
    QString placeholderText() const;
    void setPlaceholderText(const QString &text);

    QSize sizeHint() const override;

signals:
    void textChanged(const QString &text);
    void editingFinished();

protected:
    void enterEvent(QEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void applyThemeAppearance(bool night);
    void formatDecimalPlaces();                   // 失焦时补齐小数位数

private:
    qreal emphasisProgress() const { return m_emphasisProgress; }
    void setEmphasisProgress(qreal progress);

    QColor themeColor(const QString &key, const QColor &fallback = QColor()) const;
    void animateEmphasisTo(qreal value);
    qreal targetEmphasis() const;
    void refreshMetrics();
    void updateAppearance();
    int scaledPx(int designPx, int min = 0) const;

    QLineEdit *m_lineEdit;                     // 内部输入框
    QPropertyAnimation *m_emphasisAnimation;   // 状态过渡动画
    qreal m_emphasisProgress;                  // 当前动画进度（0=base, 1=focus）
    bool m_hovered;                            // 鼠标悬停
    bool m_focused;                            // 内部输入框是否持有焦点
    bool m_cursorOverridden = false;           // 是否已设置全局禁止光标覆盖
};
