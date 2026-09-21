#pragma once

#include <QLabel>
#include <QPixmap>
#include <QWidget>

/**
 * @brief 告警统计面板项控件
 *
 * 绘制圆角边框（样式同 CollMonitorMenu），左侧 32×32 图标，右侧上下两行文字。
 * 支持主题切换时自动更新颜色。
 */
class AlarmPanel : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief 构造告警面板项
     * @param imagePath  图标资源路径
     * @param labelText  顶部标签文字
     * @param numberColorPath  数字颜色在主题中的路径
     * @param parent      父控件
     */
    explicit AlarmPanel(const QString &imagePath,
                        const QString &labelText,
                        const QString &numberColorPath,
                        QWidget *parent = nullptr);

    /**
     * @brief 获取数字标签指针，用于外部更新数值
     */
    QLabel *numberLabel() const;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    /**
     * @brief 从主题读取颜色并应用到标签
     */
    void applyThemeColors();

    QString m_numberColorPath;
    QWidget *m_iconBg;
    QLabel *m_iconLabel;
    QLabel *m_topLabel;
    QLabel *m_numLabel;
};
