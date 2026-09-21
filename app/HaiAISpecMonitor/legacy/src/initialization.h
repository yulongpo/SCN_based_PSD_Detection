#pragma once

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QConicalGradient>
#include <QFrame>
#include <QPixmap>
#include <QTimer>

class QPropertyAnimation;  // 前向声明，用于进度条动画

// 1. 自定义高科技渐变进度条
class GlowProgressBar : public QWidget {
    Q_OBJECT
    Q_PROPERTY(int value READ value WRITE setValue)  // 支持QPropertyAnimation动画
public:
    explicit GlowProgressBar(QWidget *parent = nullptr);
    void setValue(int value);
    int value() const;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    int m_value;
};

// 2. 状态图标（黑勾 + 顺滑渐变加载圆环）
class TaskIconWidget : public QWidget {
    Q_OBJECT
public:
    enum State { Completed, InProgress, Pending, Failed };
    explicit TaskIconWidget(State state, QWidget *parent = nullptr);
    void setState(State state);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    State m_state;
    QTimer *m_timer;
    int angle;
};

// 3. 副标题长装饰线（两侧横向延长 + 主题渐变色）
class SubtitleDecorationWidget : public QWidget {
    Q_OBJECT
public:
    explicit SubtitleDecorationWidget(const QString &text, QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_text;
};

// 4. 超清晰矢量六边形 Icon
class HexIconWidget : public QWidget {
    Q_OBJECT
public:
    explicit HexIconWidget(QWidget *parent = nullptr);
protected:
    void paintEvent(QPaintEvent *event) override;
};

// 5. 底部渐变色状态文本与圆圈（精简小字号）
class GradientStatusWidget : public QWidget {
    Q_OBJECT
public:
    explicit GradientStatusWidget(const QString &text, QWidget *parent = nullptr);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_text;
};

// 6. 主加载弹窗窗口
class LoadingDialog : public QWidget {
    Q_OBJECT

public:
    explicit LoadingDialog(QWidget *parent = nullptr);

    void setProgress(int progress);
    void updateSystemStatus(TaskIconWidget::State state);
    void updateSpectrumStatus(TaskIconWidget::State state);
    void updateSignalStatus(TaskIconWidget::State state);
    void updateAlertStatus(TaskIconWidget::State state);
    void updateRunningStatus(TaskIconWidget::State state);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void setupUi();
    QWidget* createProgressBarBox();
    QWidget* createTaskListWidget();
    QWidget* createStepRowWidget(TaskIconWidget **icon, QLabel **nameLabel, QLabel **statusLabel, const QString &name, const QString &statusText);
    void updateStepRowWidget(TaskIconWidget *icon, QLabel *nameLabel, QLabel *statusLabel, TaskIconWidget::State state);

private:
    GlowProgressBar     *m_progressBar;
    QLabel              *m_percentLabel;
    QPropertyAnimation  *m_progressAnimation = nullptr;  // 进度条平滑动画

    // 初始化系统环境
    TaskIconWidget      *m_iconSystem;
    QLabel              *m_nameLabelSystem;
    QLabel              *m_statusLabelSystem;
    // 初始化频谱采集功能
    TaskIconWidget      *m_iconSpectrum;
    QLabel              *m_nameLabelSpectrum;
    QLabel              *m_statusLabelSpectrum;
    // 初始化信号检测功能
    TaskIconWidget      *m_iconSignal;
    QLabel              *m_nameLabelSignal;
    QLabel              *m_statusLabelSignal;
    // 初始化告警与数据管理功能
    TaskIconWidget      *m_iconAlert;
    QLabel              *m_nameLabelAlert;
    QLabel              *m_statusLabelAlert;
    // 检查系统运行状态
    TaskIconWidget      *m_iconRunning;
    QLabel              *m_nameLabelRunning;
    QLabel              *m_statusLabelRunning;
};
