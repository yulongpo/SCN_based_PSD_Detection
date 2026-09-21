#pragma once

#include <QWidget>
#include <QColor>
#include <QElapsedTimer>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QIcon>
#include <QPoint>
#include <QRect>
#include <QObject>
#include <QToolButton>
#include <QVector>
#include <QButtonGroup>

class QGraphicsOpacityEffect;
class QPropertyAnimation;
class HQComboBox;
class HQLineEdit;
class HQSpinBox;
class HQCComboBox;

class HoverTinter : public QObject
{
    Q_OBJECT

public:
    HoverTinter(QPushButton *button, const QColor &hoverColor,
                int duration, QObject *parent = nullptr);

    void setHoverColor(const QColor &hoverColor);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void syncGeometry();

    QPushButton *m_button;
    QWidget *m_overlay;
    QGraphicsOpacityEffect *m_effect;
    QPropertyAnimation *m_anim;
};

class CustomTitleBar : public QWidget
{
    Q_OBJECT

public:
    explicit CustomTitleBar(QWidget *parent = nullptr);
    ~CustomTitleBar() override;

    void setWindowTitle(const QString &title);
    void setMaximized(bool maximized);

signals:
    void minimizeClicked();
    void maximizeRestoreClicked();
    void closeClicked();
    void comboDetailClicked(const QString &comboKey, int index, const QString &text);

    void collectClicked();
    void playbackClicked();
    void settingClicked();

public slots:
    void applyThemeAppearance(bool night);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void setupUi();
    void loadIcons();
    void updateButtonIcons(bool night);
    void updateHoverTintColors(bool night);
    // 创建一个带文字（可选）、tooltip 和双态图标的导航按钮。
    // 文字在图标右侧；iconOnly 时只显示图标且尺寸更大。
    QToolButton* createNavButton(const QString& iconPath,
                                 const QString& hoverIconPath,
                                 const QString& text);

private:
    QLabel *m_iconLabel;                        // logo
    QLabel *m_titleLabel;                       // 标题
    QPushButton *m_minButton;                   // 最小化按钮
    QPushButton *m_maxButton;                   // 最大化按钮
    QPushButton *m_closeButton;                 // 关闭按钮
    QHBoxLayout *m_layout;                      // 主布局
    HoverTinter *m_minHoverTinter;              // 最小化按钮的悬浮层
    HoverTinter *m_maxHoverTinter;              // 最大化按钮的悬浮层
    HoverTinter *m_closeHoverTinter;            // 关闭按钮的悬浮层

    QButtonGroup *m_navGroup;
    QToolButton *m_CollectBtn;
    QToolButton *m_playbackBtn;
    QToolButton *m_settingBtn;

    QPixmap m_minPixmap;                        // 最小化按钮图片
    QPixmap m_maxPixmap;                        // 最大化按钮图片
    QPixmap m_closePixmap;                      // 关闭按钮图片

    bool m_isMaximized;
    bool m_isNightMode;
    QColor m_backgroundColor;
};
