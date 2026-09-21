#pragma once

#include <QPushButton>
#include <QPropertyAnimation>

/**
 * @brief 自绘按钮，支持 Primary/Error 风格，内置开始/暂停/停止图标
 */
class HQButton : public QPushButton
{
    Q_OBJECT
    Q_PROPERTY(qreal emphasisProgress READ emphasisProgress WRITE setEmphasisProgress)

public:
    enum Style { Primary, Error, Primary_normal };
    Q_ENUM(Style)

    enum Icon { NoIcon, Start, Pause, Stop };
    Q_ENUM(Icon)

    explicit HQButton(QWidget *parent = nullptr);

    void setButtonStyle(Style style);
    Style buttonStyle() const { return m_style; }

    /** 设置内置图标 */
    void setIconType(Icon icon);
    Icon iconType() const { return m_iconType; }

protected:
    void enterEvent(QEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void changeEvent(QEvent *event) override;

private slots:
    void applyThemeAppearance(bool night);

private:
    qreal emphasisProgress() const { return m_emphasisProgress; }
    void setEmphasisProgress(qreal progress);

    QColor themeColor(const QString &key, const QColor &fallback = QColor()) const;
    void animateEmphasisTo(qreal value);
    qreal targetEmphasis() const;

    void drawStartIcon(QPainter &painter, const QRectF &rect);
    void drawPauseIcon(QPainter &painter, const QRectF &rect);
    void drawStopIcon(QPainter &painter, const QRectF &rect);

    Style m_style;
    Icon m_iconType;
    QPropertyAnimation *m_emphasisAnimation;
    qreal m_emphasisProgress;
    bool m_hovered;
    bool m_pressed;
};
