#pragma once

#include <QObject>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QList>
#include <QTimer>

class QPropertyAnimation;

// ============================================================
// ToastItem — 单条提示条
// ============================================================
class ToastItem : public QFrame
{
    Q_OBJECT
public:
    enum Type { Info, Warning, Error };

    ToastItem(QWidget *parent, Type type, const QString &text, int durationMs);
    ~ToastItem() override = default;

    /// 从右侧滑入
    void slideIn();

    /// 淡出关闭
    void closeAnimated();

protected:
    /// 每次显示时自动提升到最顶层
    void showEvent(QShowEvent *event) override;

signals:
    void closed(ToastItem *item);

private slots:
    void onTimeout();

private:
    QPixmap generateIcon(Type type) const;

    QLabel      *m_iconLabel  = nullptr;
    QLabel      *m_textLabel  = nullptr;
    QPushButton *m_closeBtn   = nullptr;
    QTimer      *m_timer      = nullptr;
    Type         m_type;
};


// ============================================================
// ToastWidget — 单例管理器，在父窗口右下角弹出提示
// ============================================================
class ToastWidget : public QObject
{
    Q_OBJECT
public:
    /// 获取单例。首次调用需传入父窗口（主窗口），后续可省略
    static ToastWidget *instance(QWidget *parentWindow = nullptr);

    void showInfo   (const QString &text, int durationMs = 3000);
    void showWarning(const QString &text, int durationMs = 3000);
    void showError  (const QString &text, int durationMs = 3000);

private:
    explicit ToastWidget(QWidget *parentWindow);
    void show(ToastItem::Type type, const QString &text, int durationMs);
    void repositionAll();
    void onToastClosed(ToastItem *item);
    bool eventFilter(QObject *obj, QEvent *event) override;

    static ToastWidget *s_instance;

    QWidget          *m_parentWindow = nullptr;
    QList<ToastItem*> m_toasts;
};
