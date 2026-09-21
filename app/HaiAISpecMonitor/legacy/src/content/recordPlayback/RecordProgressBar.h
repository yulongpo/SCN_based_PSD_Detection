#pragma once

#include <QWidget>
#include <QString>
#include <QList>
#include <QTimer>
#include <QVector>

/**
 * @brief 自定义回放进度条控件
 *
 * 支持拖拽进度、加载范围指示和告警区间标记。
 * 一般告警画在轨道上方，严重告警画在轨道下方，均为横向线段表示起止时间范围。
 */
class RecordProgressBar : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param parent 父控件
     */
    explicit RecordProgressBar(QWidget *parent = nullptr);

    /** @brief 设置总时长（秒），仅用于右侧时间文本显示 */
    void setTotalTime(int seconds);
    /** @brief 设置总帧数，用于进度条像素 ↔ 帧索引映射 */
    void setTotalFrames(int frames);
    /** @brief 设置当前进度（帧索引） */
    void setPosition(int frame);
    /** @brief 设置轨道左右额外边距（像素），默认 0 */
    void setSideMargin(int pixels);
    /** @brief 设置暂停/恢复状态（true=暂停，false=播放中） */
    void setPaused(bool paused);
    /** @brief 获取当前暂停状态 */
    bool isPaused() const;

signals:
    /** @brief 用户拖拽进度时发出，参数为帧索引 */
    void positionChanged(int frame);
    /** @brief 用户点击暂停/恢复按钮时发出，参数为切换后的暂停状态 */
    void pauseToggled(bool paused);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    bool event(QEvent *event) override;

private:
    /** @brief 获取进度条轨道矩形 */
    QRectF trackRect() const;
    /** @brief 像素坐标 → 帧索引（使用帧数线性映射） */
    qreal  posToValue(qreal x) const;
    /** @brief 帧索引 → 像素坐标（使用帧数线性映射） */
    qreal  valueToPos(qreal val) const;
    /** @brief 绘制进度顶部的拖动*/
    void drawThumb(QPainter* painter, qreal thumbX, qreal thumbY) const;
    /** @brief 获取暂停/恢复按钮的矩形区域 */
    QRectF buttonRect() const;

private:
    int  m_totalTime  = 0;   ///< 总时长（秒），仅用于右侧时间文本显示
    int  m_totalFrames = 0;  ///< 总帧数，用于进度条像素 ↔ 帧索引映射
    int  m_sideMargin = 0;
    bool m_isPaused   = false;  // 暂停状态：true=暂停中，false=播放中
    int  m_currentPos = 0;    // 当前帧索引（也用于动画插值）
    bool m_dragging   = false;

    // 固定颜色
    const QColor m_trackColor    = QColor(28, 28, 42);
    const QColor m_loadedColor   = QColor(10, 140, 254);
    const QColor m_thumbColor    = QColor(255, 255, 255);
    const QColor m_timeColor     = QColor(157, 173, 190);
    const QColor m_generalColor  = QColor(255, 186, 0);
    const QColor m_severeColor   = QColor(230, 62, 62);
    // 拖动圆圈动画
    QTimer *m_animTimer;
    float m_rotateAngle = 0;
    float m_breathe = 1.0;

    // 粒子动画状态（mutable因在const drawThumb中更新，仅影响视觉表现）
    static constexpr int kParticleCount = 8;
    mutable QVector<float> m_particlePhases;    // 每个粒子的当前相位(0→1平滑递增)
    mutable QVector<float> m_particleSpeeds;    // 每个粒子的扩散速度(随机，各不相同)
    mutable QVector<float> m_particleAngles;    // 每个粒子的飞出方向角(固定)
    mutable QVector<float> m_particleMaxDists;  // 每个粒子的最大扩散距离因子(随机)
};
