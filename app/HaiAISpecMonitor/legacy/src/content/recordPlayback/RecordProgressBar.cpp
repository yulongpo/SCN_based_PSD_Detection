#include "RecordProgressBar.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QHelpEvent>
#include <QTime>
#include "comm/CommonMacros.h"
#include "comm/ScreenScale.h"
#include "comm/FontManager.h"

RecordProgressBar::RecordProgressBar(QWidget *parent)
    : QWidget(parent)
{
    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

    setMouseTracking(true);
    setFixedHeight(DPR_INT(52 * scale, dpr, 0));

    m_animTimer=new QTimer(this);

    connect(m_animTimer,&QTimer::timeout,this,[=]
    {
        m_rotateAngle+=0.05;

        m_breathe=1.0+0.08*sin(m_rotateAngle*2);

        update();
    });

    m_animTimer->start(33);

    // 初始化粒子状态：随机种子 + 每个粒子独立的初始参数
    qsrand(static_cast<uint>(QTime::currentTime().msecsSinceStartOfDay()));
    m_particlePhases.resize(kParticleCount);
    m_particleSpeeds.resize(kParticleCount);
    m_particleAngles.resize(kParticleCount);
    m_particleMaxDists.resize(kParticleCount);
    for (int i = 0; i < kParticleCount; i++) {
        m_particlePhases[i]   = (qrand() % 1000) / 1000.0f;
        m_particleSpeeds[i]   = 0.004f + 0.006f * (qrand() % 1000) / 1000.0f;
        m_particleAngles[i]   = static_cast<float>(i * M_PI_2 / 2.0);
        m_particleMaxDists[i] = 0.6f + 0.8f * (qrand() % 1000) / 1000.0f;
    }
}

void RecordProgressBar::setTotalTime(int seconds)
{
    // 仅存储总时长（秒），用于右侧时间文本 HH:mm:ss 显示，不参与进度条映射
    m_totalTime = qMax(seconds, 0);
    update();
}

void RecordProgressBar::setTotalFrames(int frames)
{
    // 设置总帧数，进度条的像素 ↔ 帧索引映射以此为基础
    m_totalFrames = qMax(frames, 0);
    if (m_currentPos > m_totalFrames) m_currentPos = m_totalFrames;
    update();
}

void RecordProgressBar::setPosition(int frame)
{
    m_currentPos = qBound(0, frame, m_totalFrames);
}

void RecordProgressBar::setSideMargin(int pixels)
{
    m_sideMargin = qMax(pixels, 0);
    update();
}

void RecordProgressBar::setPaused(bool paused)
{
    if (m_isPaused != paused) {
        m_isPaused = paused;
        update();  // 重绘按钮图标
    }
}

bool RecordProgressBar::isPaused() const
{
    return m_isPaused;
}

QRectF RecordProgressBar::trackRect() const
{
    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

    QFontMetrics fm(FontManager::instance().font(DPR_REAL(13 * scale, dpr)));
    const qreal timeW    = fm.horizontalAdvance(QStringLiteral("00:00:00"));
    const qreal gap      = DPR_INT(6 * scale, dpr, 0);  // 时间与轨道/按钮的间距
    const qreal buttonW  = DPR_INT(20 * scale, dpr, 0); // 暂停/恢复按钮宽度
    const qreal buttonGap = DPR_INT(12 * scale, dpr, 0); // 按钮与轨道的间距
    // 轨道左边界 = 左边距 + 时间文本 + 间距 + 按钮 + 间距
    const qreal ml = m_sideMargin + gap + buttonW + buttonGap;
    const qreal mr = m_sideMargin + timeW + gap;        // 轨道右边界（对称）
    const qreal h  = DPR_INT(8 * scale, dpr, 0);
    const qreal y  = (height() - h) / 2.0;
    return QRectF(ml, y, width() - ml - mr, h);
}

QRectF RecordProgressBar::buttonRect() const
{
    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

    const qreal gap     = DPR_INT(6 * scale, dpr, 0);
    const qreal buttonW = DPR_INT(20 * scale, dpr, 0);
    // 按钮位于时间文本右侧，垂直居中
    const qreal x = m_sideMargin + gap;
    const qreal y = (height() - buttonW) / 2.0;
    return QRectF(x, y, buttonW, buttonW);
}

qreal RecordProgressBar::posToValue(qreal x) const
{
    QRectF tr = trackRect();
    if (tr.width() <= 0) return 0;
    qreal frac = (x - tr.left()) / tr.width();
    // 像素位置 → 帧索引：按帧数线性映射，支持拖拽时的亚帧精度
    return qBound(0.0, frac * m_totalFrames, (qreal)m_totalFrames);
}

qreal RecordProgressBar::valueToPos(qreal val) const
{
    QRectF tr = trackRect();
    if (m_totalFrames <= 0) return tr.left();
    // 帧索引 → 像素位置：按帧数线性映射
    qreal frac = qBound(0.0, val / m_totalFrames, 1.0);
    return tr.left() + frac * tr.width();
}

void RecordProgressBar::drawThumb(QPainter* painter, qreal thumbX, qreal thumbY) const
{
    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

    const QPointF c(thumbX, thumbY);
    const qreal baseR = DPR_INT(5.0 * m_breathe, dpr, 0); //核心半径
    const qreal glowR = DPR_INT(10.0 * m_breathe, dpr, 0); //主光晕半径

    // 1. 中心
    QRadialGradient g(c,5*m_breathe);
    g.setColorAt(0,QColor(255,255,255));
    g.setColorAt(0.4,QColor(180,230,255));
    g.setColorAt(1,QColor(100,180,255,0));
    painter->setPen(Qt::NoPen);
    painter->setBrush(g);
    painter->drawEllipse(c,5*m_breathe,5*m_breathe);

    // 2.主光晕 — 多层呼吸光晕，每层有独立的相位偏移，形成波纹扩散效果
    for (int i = 0; i < 4; i++)
    {
        // 每层光晕独立相位：m_rotateAngle驱动 + i偏移让各层错开，避免同步跳动
        qreal phase = m_rotateAngle * 0.5 + i * 0.7;
        // 半径平滑脉动：正弦波动±1像素，让光环有呼吸感
        qreal waveR = sin(phase) * DPR_INT(1.0, dpr, 0);
        qreal r = glowR + i * DPR_INT(2.0, dpr, 0) + waveR;
        // 透明度平滑波动：基础衰减叠加上下20%的正弦呼吸
        qreal alphaFactor = 0.8 + 0.2 * sin(phase * 1.2);
        int alpha = qMax(0, int((60 - i * 15) * alphaFactor));
        QColor cc(60, 160, 255, alpha);
        painter->setBrush(cc);
        painter->drawEllipse(c,
                             r,
                             r);
    }

    // 3.粒子 — 由内向外扩散，每个粒子独立推进相位，飞完随机重置
    painter->setBrush(Qt::NoBrush);
    for (int i = 0; i < kParticleCount; i++)
    {
        // 每个粒子独立推进相位，速度各不相同，飞完自动重置
        m_particlePhases[i] += m_particleSpeeds[i];
        if (m_particlePhases[i] >= 1.0f) {
            // 粒子飞到最外层，用随机参数重置，重新从中心出发
            m_particlePhases[i]   = 0.0f;
            m_particleSpeeds[i]   = 0.004f + 0.006f * (qrand() % 1000) / 1000.0f;
            m_particleMaxDists[i] = 0.6f + 0.8f * (qrand() % 1000) / 1000.0f;
        }

        qreal phase = m_particlePhases[i];
        // 粒子到中心的距离：从0平滑扩散到最大距离
        qreal dist = phase * DPR_INT(10.0, dpr, 0) * 4 * m_particleMaxDists[i];
        // 飞行方向：固定角度 + m_rotateAngle缓慢旋转
        qreal angle = m_rotateAngle * 0.2 + m_particleAngles[i];
        QPointF pos(c.x() + dist * cos(angle), c.y() + dist * sin(angle));
        // 透明度随距离平方衰减
        qreal alpha = (1.0 - phase) * (1.0 - phase);
        int a = qBound(10, int(alpha * 220), 220);
        QColor pc(160, 225, 255, a);
        painter->setPen(Qt::NoPen);
        painter->setBrush(pc);
        qreal pr = qMax<qreal>(DPR_REAL(1.5, dpr),
                              DPR_REAL(3.0, dpr) * (1.0 - phase * 0.5));
        painter->drawEllipse(pos, pr, pr);
    }
}

void RecordProgressBar::paintEvent(QPaintEvent * /*event*/)
{
    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    QRectF tr = trackRect();
    const qreal trackH = DPR_INT(8 * scale, dpr, 0);
    const qreal cy     = tr.center().y();   // 轨道中心纵坐标

    // ---- 时间文字（自动计算宽度，紧贴轨道） ----
    // 将秒数转为 "HH:mm:ss" 格式，显式补零避免 QTime::toString 格式解析歧义
    auto timeStr = [](int sec) -> QString {
        const int totalSecs = qMax(sec, 0);
        const int hours   = totalSecs / 3600;
        const int minutes = (totalSecs % 3600) / 60;
        const int seconds = totalSecs % 60;
        return QString("%1:%2:%3")
            .arg(hours,   2, 10, QChar('0'))
            .arg(minutes, 2, 10, QChar('0'))
            .arg(seconds, 2, 10, QChar('0'));
    };

    QFontMetrics fm(FontManager::instance().font(DPR_INT(13 * scale, dpr, 0)));
    const int timeW = fm.horizontalAdvance(QStringLiteral("00:00:00"));

    const QString leftText  = timeStr(0);// 固定为0
    const QString rightText = timeStr(m_totalTime);

    p.setPen(m_timeColor);
    p.setFont(FontManager::instance().font(DPR_INT(13 * scale, dpr, 0)));

    // 右时间（紧贴轨道右侧）
    QRectF rr(width() - timeW - m_sideMargin, 0, timeW, height());
    p.drawText(rr, Qt::AlignRight | Qt::AlignVCenter, rightText);

    // ---- 暂停/恢复按钮（位于左时间文本与轨道之间） ----
    {
        QRectF btnRect = buttonRect();
        p.save();
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(m_timeColor);

        const qreal cx = btnRect.center().x();
        const qreal cy = btnRect.center().y();
        if (m_isPaused) {
            // 暂停状态 → 绘制播放三角形（▶），点击可恢复播放
            const qreal halfW = btnRect.width() * 0.30;   // 三角形半宽
            const qreal halfH = btnRect.height() * 0.35;  // 三角形半高
            QPainterPath triangle;
            triangle.moveTo(cx - halfW, cy - halfH);      // 左上顶点
            triangle.lineTo(cx + halfW, cy);              // 右顶点（尖端）
            triangle.lineTo(cx - halfW, cy + halfH);      // 左下顶点
            triangle.closeSubpath();
            p.drawPath(triangle);
        } else {
            // 播放状态 → 绘制暂停双竖线（⏸），点击可暂停
            const qreal barW  = btnRect.width() * 0.20;   // 单条竖线宽度
            const qreal barH  = btnRect.height() * 0.50;  // 竖线高度
            const qreal barY  = cy - barH / 2.0;          // 竖线顶部Y坐标
            const qreal gapX  = btnRect.width() * 0.10;   // 两条竖线之间的半间距
            const qreal leftX = cx - gapX - barW;          // 左竖线X坐标
            const qreal rightX = cx + gapX;                // 右竖线X坐标
            p.drawRect(QRectF(leftX, barY, barW, barH));
            p.drawRect(QRectF(rightX, barY, barW, barH));
        }
        p.restore();
    }

    // ---- 进度条背景轨道 ----
    QPainterPath bgPath;
    bgPath.addRoundedRect(QRectF(tr.left(), cy - trackH / 2.0, tr.width(), trackH),
                           trackH / 2.0, trackH / 2.0);
    p.fillPath(bgPath, m_trackColor);

    // ---- 已加载进度（使用浮点动画位置，实现像素级平滑移动） ----
    if (m_currentPos > 0) {
        qreal endX = valueToPos(m_currentPos);
        qreal loadedW = endX - tr.left();
        if (loadedW > 0) {
            QPainterPath loadedPath;
            loadedPath.addRoundedRect(QRectF(tr.left(), cy - trackH / 2.0, loadedW, trackH),
                                      trackH / 2.0, trackH / 2.0);
            p.fillPath(loadedPath, m_loadedColor);
        }
    }

    // ---- 拖动矩形（白色圆角矩形） ----
    {
        qreal thumbX = valueToPos(m_currentPos);
        qreal thumbW = DPR_INT(6 * scale, dpr, 0);
        qreal thumbH = DPR_INT(22 * scale, dpr, 0);
        qreal thumbY = cy - thumbH / 2.0;

        // QPainterPath thumbPath;
        // thumbPath.addRoundedRect(
        //     QRectF(thumbX - thumbW / 2.0, thumbY, thumbW, thumbH), DPR_INT(3 * scale, dpr, 0), DPR_INT(3 * scale, dpr, 0));
        // p.fillPath(thumbPath, m_thumbColor);
        drawThumb(&p, thumbX, cy);
    }
}

// ============================================================================
// 鼠标事件 — 拖拽进度
// ============================================================================

void RecordProgressBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    // 优先检查是否点击了暂停/恢复按钮
    QRectF btnRect = buttonRect();
    if (btnRect.contains(event->pos())) {
        m_isPaused = !m_isPaused;
        emit pauseToggled(m_isPaused);
        update();
        return;
    }

    if (m_isPaused)
    {
        // 进入拖拽预览模式：只更新视觉位置，信号延迟到 mouseReleaseEvent 发送
        // 避免 press + move 过程中反复触发昂贵的文件读取
        m_dragging = true;
        int frame = qRound(posToValue(event->pos().x()));
        m_currentPos = qBound(0, frame, m_totalFrames);
        update();
    }
}

void RecordProgressBar::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging) {
        // 拖拽过程中只更新视觉位置，不发送信号
        // 避免每次像素移动都触发昂贵的 seek 文件读取操作
        int frame = qRound(posToValue(event->pos().x()));
        m_currentPos = qBound(0, frame, m_totalFrames);
        update();
    }
    if (buttonRect().contains(event->pos())) {
        setCursor(Qt::PointingHandCursor);
    }
    else
    {
        setCursor(Qt::ArrowCursor);
    }
    QWidget::mouseMoveEvent(event);
}

void RecordProgressBar::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        int frame = qRound(posToValue(event->pos().x()));
        m_currentPos = qBound(0, frame, m_totalFrames);
        emit positionChanged(m_currentPos);  // 发射帧索引
        update();
    }
    QWidget::mouseReleaseEvent(event);
}

bool RecordProgressBar::event(QEvent *event)
{
    return QWidget::event(event);
}
