#pragma once

#include <QWidget>
#include <QTimer>
#include <QVector>

class LoadingWidget : public QWidget
{
    Q_OBJECT

public:
    explicit LoadingWidget(QWidget *parent = nullptr);
    ~LoadingWidget() override;

    // 开启与关闭加载动画
    void startLoading();
    void stopLoading();

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void initTracePoints(); // 初始化模拟谱线数据
    void centerOnParent();  // 自动居中定位

private slots:
    void updateAnimation();

private:
    QTimer *m_timer;
    double m_sweepX;                 // 扫频线当前 X 坐标
    const double m_sweepSpeed = 2.0; // 扫频速度

    QVector<double> m_tracePoints;   // 存储谱线各点 Y 坐标
    QVector<double> m_pixelAges;     // 存储各点亮度衰减因子 (0.15 - 1.0)
};
