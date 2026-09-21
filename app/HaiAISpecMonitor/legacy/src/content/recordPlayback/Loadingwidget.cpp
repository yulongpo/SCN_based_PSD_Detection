#include "LoadingWidget.h"
#include <QPainter>
#include <QEvent>
#include <QPolygonF>
#include <cmath>

#include "comm/CommonMacros.h"
#include "comm/ScreenScale.h"

LoadingWidget::LoadingWidget(QWidget *parent)
    : QWidget(parent)
    , m_sweepX(0.0)
{
    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();
    // 设置 200x140 的紧凑精细尺寸，完美契合专业仪器面板
    setFixedSize(DPR_INT(200 * scale, dpr, 0), DPR_INT(140 * scale, dpr, 0));

    // 设置无边框与背景透明
    setWindowFlags(Qt::FramelessWindowHint | Qt::SubWindow);
    setAttribute(Qt::WA_TranslucentBackground);

    // 初始化谱线数据
    initTracePoints();

    // 动画驱动定时器 (刷新率约 40 帧/秒)
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &LoadingWidget::updateAnimation);

    // 自动监听父窗口
    if (parent) {
        parent->installEventFilter(this);
    }
}

LoadingWidget::~LoadingWidget()
{
}

void LoadingWidget::initTracePoints()
{
    int w = width();
    int h = height();
    double noiseFloor = h - 35; // 数据显示底线

    m_tracePoints.resize(w);
    m_pixelAges.resize(w);

    for (int x = 0; x < w; ++x) {
        // 1. 基础热噪声模拟
        double y = noiseFloor + (std::sin(x * 0.5) * 1.5) + (std::cos(x * 0.9) * 1.0);

        // 2. 模拟通道内存在的主信号峰 1 (30% 频率位置)
        double peak1Pos = w * 0.3;
        double dist1 = std::abs(x - peak1Pos);
        if (dist1 < 12) {
            y -= std::exp(-std::pow(dist1 / 4.0, 2)) * 55.0;
        }

        // 3. 模拟辅信号峰 2 (70% 频率位置)
        double peak2Pos = w * 0.7;
        double dist2 = std::abs(x - peak2Pos);
        if (dist2 < 18) {
            y -= std::exp(-std::pow(dist2 / 6.0, 2)) * 30.0;
        }

        m_tracePoints[x] = y;
        m_pixelAges[x] = 0.15; // 默认环境底噪谱线亮度
    }
}

void LoadingWidget::startLoading()
{
    m_sweepX = 0.0;
    // 重置辉光状态
    for (int i = 0; i < m_pixelAges.size(); ++i) {
        m_pixelAges[i] = 0.15;
    }
    m_timer->start(15); // 15ms 刷新
    centerOnParent();
    show();
}

void LoadingWidget::stopLoading()
{
    m_timer->stop();
    hide();
}

void LoadingWidget::centerOnParent()
{
    QWidget *parent = parentWidget();
    if (parent) {
        int x = (parent->width() - width()) / 2;
        int y = (parent->height() - height()) / 2;
        move(x, y);
    }
}

void LoadingWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    centerOnParent();
}

bool LoadingWidget::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == parentWidget()) {
        if (event->type() == QEvent::Resize || event->type() == QEvent::Show) {
            centerOnParent();
        }
    }
    return QWidget::eventFilter(obj, event);
}

void LoadingWidget::updateAnimation()
{
    int w = width();

    // 扫频线右移
    m_sweepX += m_sweepSpeed;
    if (m_sweepX >= w) {
        m_sweepX = 0.0;
    }

    // 更新各个频点的荧光年龄（即信号线衰减逻辑）
    for (int x = 0; x < w; ++x) {
        if (x == static_cast<int>(m_sweepX)) {
            m_pixelAges[x] = 1.0; // 被扫频线扫到时，亮度瞬间达到 100%
        } else {
            m_pixelAges[x] *= 0.96; // 随时间流逝像余晖一样缓慢衰减
            if (m_pixelAges[x] < 0.15) {
                m_pixelAges[x] = 0.15; // 保持暗青色作为背景底线
            }
        }
    }

    update(); // 触发重绘 paintEvent
}

void LoadingWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing); // 开启抗锯齿以获得精密线条

    int w = width();
    int h = height();

    // 1. 绘制暗色仪器底色 (保持与主图表一致，稍微带一点半透明)
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(6, 10, 19, 210));
    painter.drawRoundedRect(rect(), 4, 4);

    // 2. 绘制 1px 极细外边框
    painter.setPen(QPen(QColor(22, 35, 53), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(0, 0, w - 1, h - 1);

    // 3. 绘制精密虚线网格 (Grid)
    QPen gridPen(QColor(16, 26, 40), 1);
    gridPen.setDashPattern({2, 3}); // 设置虚线间距
    painter.setPen(gridPen);

    // 绘制 4 列
    for (int i = 1; i < 4; ++i) {
        int gridX = (w / 4) * i;
        painter.drawLine(gridX, 0, gridX, h);
    }
    // 绘制 3 行
    for (int i = 1; i < 3; ++i) {
        int gridY = (h / 3) * i;
        painter.drawLine(0, gridY, w, gridY);
    }

    // 4. 绘制渐变衰减的频谱波形 (Trace Line)
    for (int x = 1; x < w; ++x) {
        double age = m_pixelAges[x];
        QColor color;
        if (age > 0.9) {
            color = QColor(0, 229, 255); // 活跃段：极高亮青色
        } else {
            // 在亮青色和暗蓝灰色之间进行插值运算
            int r = static_cast<int>(0 * age + 15 * (1.0 - age));
            int g = static_cast<int>(229 * age + 35 * (1.0 - age));
            int b = static_cast<int>(255 * age + 53 * (1.0 - age));
            color = QColor(r, g, b);
        }

        painter.setPen(QPen(color, 1.2));
        painter.drawLine(QPointF(x - 1, m_tracePoints[x - 1]), QPointF(x, m_tracePoints[x]));
    }

    // 5. 绘制垂直扫频触发线
    painter.setPen(QPen(QColor(0, 229, 255, 150), 1));
    painter.drawLine(QPointF(m_sweepX, 0), QPointF(m_sweepX, h));

    // 绘制扫频线顶部的倒三角小指针
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 229, 255));
    QPolygonF triangle;
    triangle << QPointF(m_sweepX - 3, 0)
             << QPointF(m_sweepX + 3, 0)
             << QPointF(m_sweepX, 4);
    painter.drawPolygon(triangle);

    // 6. 绘制极简的加载提示字样
    painter.setPen(QColor(74, 92, 117)); // 浅蓝灰
    QFont font = painter.font();
    font.setPointSize(9);
    font.setFamily("Segoe UI");
    painter.setFont(font);
    painter.drawText(QRect(0, h - 30, w, 25), Qt::AlignCenter, u8"BB60C 数据载入中...");
}