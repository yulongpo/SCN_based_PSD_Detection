#pragma once

#include <QWidget>

/**
 * @brief 滑动开关控件
 *
 * 圆角胶囊形状开关，支持右侧文字标签，可自定义开关颜色。
 * 默认开启颜色 rgb(10,140,254)，关闭背景 rgb(92,92,92)，滑块为白色圆形。
 */
class HQSwitch : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief 构造滑动开关
     * @param text 右侧标签文字
     * @param parent 父控件
     */
    explicit HQSwitch(const QString &text = QString(), QWidget *parent = nullptr);

    /**
     * @brief 是否开启
     */
    bool isOn() const { return m_on; }

    /**
     * @brief 设置开关状态
     * @param on 是否开启
     */
    void setOn(bool on);

    /**
     * @brief 获取/设置右侧标签文字
     */
    QString text() const { return m_text; }
    void setText(const QString &text);

    /**
     * @brief 设置开启时背景色
     * @param color 背景色
     */
    void setOnColor(const QColor &color);

    /**
     * @brief 设置关闭时背景色
     * @param color 背景色
     */
    void setOffColor(const QColor &color);

    QSize sizeHint() const override;

signals:
    /** 开关状态改变信号 */
    void toggled(bool on);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QString m_text;             ///< 右侧标签文字
    bool    m_on       = false; ///< 是否开启

    QColor m_onColor  = QColor(10, 140, 254);  ///< 开启背景色
    QColor m_offColor = QColor(92, 92, 92);     ///< 关闭背景色
};
