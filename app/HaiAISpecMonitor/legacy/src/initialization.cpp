#include "initialization.h"
#include <cmath>
#include <QPropertyAnimation>
#include <QVariantAnimation>
#include "comm/CommonMacros.h"
#include "comm/ScreenScale.h"

GlowProgressBar::GlowProgressBar(QWidget *parent) : QWidget(parent), m_value(68) {
    const auto &ss       = ScreenScale::instance();
    const qreal dpr      = ss.dpr();
    const double scale   = ss.scale();
    setFixedHeight(DPR_INT(20 * scale, dpr, 0));
}

void GlowProgressBar::setValue(int value) {
    m_value = qBound(0, value, 100);
    update();
}

int GlowProgressBar::value() const {
    return m_value;
}

void GlowProgressBar::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const auto &ss       = ScreenScale::instance();
    const qreal dpr      = ss.dpr();
    const double scale   = ss.scale();

    QRectF trackRect = rect();

    int xp = DPR_INT(2 * scale, dpr, 0);
    QRectF innerTrack = trackRect.adjusted(xp, xp, -xp, -xp);
    // 1. 轨道边框
    painter.setPen(QPen(QColor("#183359"), DPR_INT(2 * scale, dpr, 0)));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(innerTrack, xp, xp);

    if (m_value <= 0) return;

    // 2. 计算 Chunk 宽度
    float chunkWidth = innerTrack.width() * (m_value / 100.0f);
    QRectF chunkRect(innerTrack.left(), innerTrack.top(), chunkWidth, innerTrack.height());

    // 3. 填充色【由暗到亮渐变】：深暗蓝(左) -> 亮蓝 -> 极亮青(右)
    QLinearGradient fillGrad(chunkRect.left(), 0, chunkRect.right(), 0);
    fillGrad.setColorAt(0.0, QColor("#002b82"));
    fillGrad.setColorAt(1.0, QColor("#18bffa"));

    painter.setPen(Qt::NoPen);
    painter.setBrush(fillGrad);
    painter.drawRoundedRect(chunkRect, xp, xp);

    // 4. 边框线【由暗到亮渐变】：暗蓝(左) -> 极高亮青蓝(右)
    QLinearGradient borderGrad(chunkRect.left(), 0, chunkRect.right(), 0);
    borderGrad.setColorAt(0.0, QColor("#0361cc"));
    borderGrad.setColorAt(1.0, QColor("#99f8fd"));

    painter.setPen(QPen(QBrush(borderGrad), xp));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(chunkRect, 2, 2);

    // 5. 头部强光与右侧拖尾残影
    float rightX = chunkRect.right();
    float topY = chunkRect.top();
    float botY = chunkRect.bottom();
    float midY = chunkRect.center().y();

    QLinearGradient headLineGrad(0, topY, 0, botY);
    headLineGrad.setColorAt(0.0, QColor(0, 229, 255, 120));
    headLineGrad.setColorAt(0.5, QColor(255, 255, 255, 255));
    headLineGrad.setColorAt(1.0, QColor(0, 229, 255, 120));

    painter.setPen(QPen(QBrush(headLineGrad), xp));
    painter.drawLine(QPointF(rightX, topY), QPointF(rightX, botY));

    QLinearGradient trailGrad1(rightX, 0, rightX + DPR_INT(16 * scale, dpr, 0), 0);
    trailGrad1.setColorAt(0.0, QColor(0, 229, 255, 220));
    trailGrad1.setColorAt(1.0, QColor(0, 229, 255, 0));

    painter.setPen(QPen(QBrush(trailGrad1), 1.0));
    painter.drawLine(QPointF(rightX, midY), QPointF(rightX + DPR_INT(16 * scale, dpr, 0), midY));

    QLinearGradient trailGrad2(rightX, 0, rightX + DPR_INT(10 * scale, dpr, 0), 0);
    trailGrad2.setColorAt(0.0, QColor(0, 229, 255, 160));
    trailGrad2.setColorAt(1.0, QColor(0, 229, 255, 0));

    painter.setPen(QPen(QBrush(trailGrad2), 1.0));
    painter.drawLine(QPointF(rightX, topY + xp), QPointF(rightX + DPR_INT(10 * scale, dpr, 0), topY + xp));
    painter.drawLine(QPointF(rightX, botY - xp), QPointF(rightX + DPR_INT(10 * scale, dpr, 0), botY - xp));
}

TaskIconWidget::TaskIconWidget(State state, QWidget *parent)
    : QWidget(parent), m_state(state) {
    const auto &ss       = ScreenScale::instance();
    const qreal dpr      = ss.dpr();
    const double scale   = ss.scale();
    setFixedSize(DPR_INT(20 * scale, dpr, 0), DPR_INT(20 * scale, dpr, 0));
    angle = 0;
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, [this]()
    {
        if (m_state == TaskIconWidget::InProgress)
        {
            angle = (angle + 15) % 360;
            update();
        }
    });
    m_timer->start(1000 / 60);
}

void TaskIconWidget::setState(State state) {
    m_state = state;
    update();
}

void TaskIconWidget::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const auto &ss       = ScreenScale::instance();
    const qreal dpr      = ss.dpr();
    const double scale   = ss.scale();

    int w = width();
    int h = height();
    int rectSize = DPR_INT(18 * scale, dpr, 0);
    QRectF rect((w - rectSize) / 2.0, (h - rectSize) / 2.0, rectSize, rectSize);

    if (m_state == Completed) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#1b8afa"));
        painter.drawEllipse(rect);

        QPainterPath path;
        path.moveTo(rect.left() + DPR_REAL(4.5 * scale, dpr), rect.top() + DPR_REAL(7.2 * scale, dpr));
        path.lineTo(rect.left() + DPR_REAL(8.0 * scale, dpr), rect.top() + DPR_REAL(12.2 * scale, dpr));
        path.lineTo(rect.left() + DPR_REAL(13 * scale, dpr), rect.top() + DPR_REAL(5.5 * scale, dpr));

        QPen checkPen(QColor("#01153a"), DPR_INT(2 * scale, dpr, 0), Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin);
        painter.setPen(checkPen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);

    } else if (m_state == InProgress) {
        QRectF arcRect = rect.adjusted(DPR_INT(1 * scale, dpr, 1), DPR_INT(1 * scale, dpr, 1), -DPR_INT(1 * scale, dpr, 1), -DPR_INT(1 * scale, dpr, 1));
        QConicalGradient conicGrad(arcRect.center(), angle);
        conicGrad.setColorAt(0.0, QColor(0, 132, 255, 20));
        conicGrad.setColorAt(0.5, QColor("#007CEE"));
        conicGrad.setColorAt(1.0, QColor("#00E5FF"));

        QPen arcPen(QBrush(conicGrad), DPR_REAL(2.0 * scale, dpr));
        painter.setPen(arcPen);
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(arcRect);

    } else if (m_state == Pending) {
        QPen dashPen(QColor("#556a86"), DPR_REAL(1.2 * scale, dpr), Qt::DashLine);
        painter.setPen(dashPen);
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(rect.adjusted(DPR_INT(1 * scale, dpr, 1), DPR_INT(1 * scale, dpr, 1), -DPR_INT(1 * scale, dpr, 1), -DPR_INT(1 * scale, dpr, 1)));
    }
    else if (m_state == Failed) {
        // 绘制红色填充圆圈
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#E74C3C"));
        painter.drawEllipse(rect);

        // 绘制白色叉号（两条交叉线段）
        QPainterPath path;
        // 第一条线：左上到右下
        path.moveTo(rect.left() + DPR_REAL(5.5 * scale, dpr), rect.top() + DPR_REAL(5.5 * scale, dpr));
        path.lineTo(rect.left() + DPR_REAL(12.5 * scale, dpr), rect.top() + DPR_REAL(12.5 * scale, dpr));
        // 第二条线：右上到左下
        path.moveTo(rect.left() + DPR_REAL(12.5 * scale, dpr), rect.top() + DPR_REAL(5.5 * scale, dpr));
        path.lineTo(rect.left() + DPR_REAL(5.5 * scale, dpr), rect.top() + DPR_REAL(12.5 * scale, dpr));

        QPen crossPen(QColor("#FFFFFF"), DPR_INT(2 * scale, dpr, 0), Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin);
        painter.setPen(crossPen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
    }
}

SubtitleDecorationWidget::SubtitleDecorationWidget(const QString &text, QWidget *parent)
    : QWidget(parent), m_text(text) {
    const auto &ss       = ScreenScale::instance();
    const qreal dpr      = ss.dpr();
    const double scale   = ss.scale();
    setFixedHeight(DPR_INT(30 * scale, dpr, 0));
}

void SubtitleDecorationWidget::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const auto &ss       = ScreenScale::instance();
    const qreal dpr      = ss.dpr();
    const double scale   = ss.scale();

    QFont font(u8"黑体", DPR_INT(9 * scale, dpr, 0));
    painter.setFont(font);
    QFontMetrics fm(font);
    int textWidth = fm.horizontalAdvance(m_text);
    int textX = (width() - textWidth) / 2;
    int centerY = height() / 2;

    // 中央文本
    painter.setPen(QColor("#9cbcef"));
    painter.drawText(textX, centerY + fm.ascent() / 2 - DPR_INT(2 * scale, dpr, 0), m_text);

    int textGap = DPR_INT(20 * scale, dpr, 0);   // 点与文本的间距
    int dotRadius = DPR_INT(3 * scale, dpr, 0);  // 点半径
    int edgeMargin = DPR_INT(25 * scale, dpr, 0);// 延伸至接近左右卡片边缘的位置

    // 左侧点与长线段（向左拉长至边缘）
    int leftDotX = textX - textGap - dotRadius;
    int leftLineEndX = leftDotX;
    int leftLineStartX = edgeMargin;

    // 右侧点与长线段（向右拉长至边缘）
    int rightDotX = textX + textWidth + textGap + dotRadius;
    int rightLineStartX = rightDotX;
    int rightLineEndX = width() - edgeMargin;

    // 左线段渐变
    QLinearGradient leftGrad(leftLineStartX, 0, leftLineEndX, 0);
    leftGrad.setColorAt(0.0, QColor(6, 101, 197, 0));
    leftGrad.setColorAt(1.0, QColor(6, 101, 197, 255));

    painter.setPen(QPen(QBrush(leftGrad), 1.0));
    painter.drawLine(leftLineStartX, centerY, leftLineEndX, centerY);

    // 右线段渐变
    QLinearGradient rightGrad(rightLineStartX, 0, rightLineEndX, 0);
    rightGrad.setColorAt(1.0, QColor(6, 101, 197, 0));
    rightGrad.setColorAt(0.0, QColor(6, 101, 197, 255));

    painter.setPen(QPen(QBrush(rightGrad), 1.0));
    painter.drawLine(rightLineStartX, centerY, rightLineEndX, centerY);

    // 亮蓝发光点
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#0582f3"));
    painter.drawEllipse(QPointF(leftDotX, centerY), dotRadius, dotRadius);
    painter.drawEllipse(QPointF(rightDotX, centerY), dotRadius, dotRadius);
}

HexIconWidget::HexIconWidget(QWidget *parent) : QWidget(parent) {
    const auto &ss       = ScreenScale::instance();
    const qreal dpr      = ss.dpr();
    const double scale   = ss.scale();
    setFixedSize(DPR_INT(24 * scale, dpr, 0), DPR_INT(24 * scale, dpr, 0));
}

void HexIconWidget::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const auto &ss       = ScreenScale::instance();
    const qreal dpr      = ss.dpr();
    const double scale   = ss.scale();

    QPolygonF hex;
    float cx = width() / 2.0f;
    float cy = height() / 2.0f;
    float r = DPR_REAL(10.0 * scale, dpr);

    for (int i = 0; i < 6; ++i) {
        float angle = i * 60.0f * 3.14159f / 180.0f;
        hex << QPointF(cx + r * cos(angle), cy + r * sin(angle));
    }

    painter.setBrush(QColor(0, 132, 255, 38));
    painter.setPen(QPen(QColor("#0084FF"), DPR_REAL(1.5 * scale, dpr)));
    painter.drawPolygon(hex);

    painter.setPen(QPen(QColor("#00D2FF"), DPR_REAL(1.5 * scale, dpr)));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QPointF(cx, cy), DPR_REAL(4 * scale, dpr), DPR_REAL(4 * scale, dpr));
}

GradientStatusWidget::GradientStatusWidget(const QString &text, QWidget *parent)
    : QWidget(parent), m_text(text) {
    const auto &ss       = ScreenScale::instance();
    const qreal dpr      = ss.dpr();
    const double scale   = ss.scale();

    QFont font(u8"黑体", DPR_INT(6 * scale, dpr, 0));
    QFontMetrics fm(font);
    int textWidth = fm.horizontalAdvance(m_text);

    // 包含左侧圆圈(8px) + 间距(5px) + 文字宽度 + 余量
    setFixedSize(DPR_INT(14 * scale, dpr, 0) + textWidth + DPR_INT(4 * scale, dpr, 0), DPR_INT(16 * scale, dpr, 0));
}

QSize GradientStatusWidget::sizeHint() const {
    return QSize(width(), height());
}

void GradientStatusWidget::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const auto &ss       = ScreenScale::instance();
    const qreal dpr      = ss.dpr();
    const double scale   = ss.scale();

    // 1. 左侧小圆圈 (8px)
    QRectF circleRect(DPR_INT(1 * scale, dpr, 1), (height() - DPR_INT(8 * scale, dpr, 0)) / 2.0, DPR_INT(8 * scale, dpr, 0), DPR_INT(8 * scale, dpr, 0));
    painter.setPen(QPen(QColor("#0084FF"), DPR_REAL(1.5 * scale, dpr)));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(circleRect);

    // 2. 8pt 渐变小字体
    QFont font(u8"黑体", DPR_INT(6 * scale, dpr, 0));
    painter.setFont(font);
    QFontMetrics fm(font);

    int textX = DPR_INT(13 * scale, dpr, 0);
    int textY = (height() + fm.ascent() - fm.descent()) / 2;

    QLinearGradient textGrad(textX, 0, textX + fm.horizontalAdvance(m_text), 0);
    textGrad.setColorAt(0.0, QColor("#0072FF"));
    textGrad.setColorAt(1.0, QColor("#00D2FF"));

    painter.setPen(QPen(QBrush(textGrad), DPR_INT(1 * scale, dpr, 1)));
    painter.drawText(textX, textY, m_text);
}

LoadingDialog::LoadingDialog(QWidget *parent) : QWidget(parent) {
    const auto &ss       = ScreenScale::instance();
    const qreal dpr      = ss.dpr();
    const double scale   = ss.scale();

    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::SplashScreen);
    setAttribute(Qt::WA_TranslucentBackground);
    // setFixedSize(DPR_INT(800 * scale, dpr, 0), DPR_INT(600 * scale, dpr, 0));
    setFixedWidth(DPR_INT(700 * scale, dpr, 0));

    setupUi();
}

void LoadingDialog::setProgress(int progress)
{
    // 停止正在进行的旧动画，防止动画冲突
    if (m_progressAnimation != nullptr) {
        m_progressAnimation->stop();
        // DeleteWhenStopped策略会自动删除旧动画对象，并触发destroyed信号将指针置null
    }

    // 创建300ms平滑动画，从当前进度值过渡到目标progress值
    auto *animation = new QPropertyAnimation(m_progressBar, "value", this);
    animation->setDuration(300);                                          // 动画时长300ms
    animation->setStartValue(m_progressBar->value());                     // 起始值：进度条当前值
    animation->setEndValue(progress);                                     // 结束值：目标progress
    animation->setEasingCurve(QEasingCurve::OutCubic);                    // 缓动曲线：减速淡出，更自然

    // 动画过程中同步更新百分比标签文本（02、03... 99、100）
    connect(animation, &QPropertyAnimation::valueChanged, this, [this](const QVariant &value) {
        int p = value.toInt();
        m_percentLabel->setText((p < 10 ? "0" + QString::number(p) : QString::number(p)) + "%");
    });

    // 动画销毁时将成员指针置null，防止悬空指针
    connect(animation, &QPropertyAnimation::destroyed, this, [this]() {
        m_progressAnimation = nullptr;
    });

    animation->start(QAbstractAnimation::DeleteWhenStopped);  // 动画结束后自动删除
    m_progressAnimation = animation;
}

void LoadingDialog::updateSystemStatus(TaskIconWidget::State state)
{
    updateStepRowWidget(m_iconSystem, m_nameLabelSystem, m_statusLabelSystem, state);
}

void LoadingDialog::updateSpectrumStatus(TaskIconWidget::State state)
{
    updateStepRowWidget(m_iconSpectrum, m_nameLabelSpectrum, m_statusLabelSpectrum, state);
}

void LoadingDialog::updateSignalStatus(TaskIconWidget::State state)
{
    updateStepRowWidget(m_iconSignal, m_nameLabelSignal, m_statusLabelSignal, state);
}

void LoadingDialog::updateAlertStatus(TaskIconWidget::State state)
{
    updateStepRowWidget(m_iconAlert, m_nameLabelAlert, m_statusLabelAlert, state);
}

void LoadingDialog::updateRunningStatus(TaskIconWidget::State state)
{
    updateStepRowWidget(m_iconRunning, m_nameLabelRunning, m_statusLabelRunning, state);
}

void LoadingDialog::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const auto &ss       = ScreenScale::instance();
    const qreal dpr      = ss.dpr();
    const double scale   = ss.scale();

    int padding = DPR_INT(10 * scale, dpr, 0);
    QRectF rect = this->rect().adjusted(padding, padding, -padding, -padding);

    // 1.【底色渐变（取自第一张原图：上部较亮夜蓝，底部极暗黑蓝）】
    QLinearGradient bgGrad(0, rect.top(), 0, rect.bottom());
    bgGrad.setColorAt(0.0, QColor("#07192F"));  // 顶部较亮星空暗蓝
    bgGrad.setColorAt(1.0, QColor("#02070F"));  // 底部深邃黑蓝

    painter.setPen(Qt::NoPen);
    painter.setBrush(bgGrad);
    painter.drawRoundedRect(rect, DPR_INT(12 * scale, dpr, 0), DPR_INT(12 * scale, dpr, 0));

    // 2.【外边框 - 半透明淡蓝】
    QPen outerPen(QColor(17, 50, 83, 100), DPR_REAL(2.0 * scale, dpr));
    painter.setPen(outerPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(rect, DPR_INT(12 * scale, dpr, 0), DPR_INT(12 * scale, dpr, 0));

    // 3.【内边框 - 从顶部亮青蓝到底部变暗】
    int innerPadding = DPR_INT(4 * scale, dpr, 0);
    QRectF innerRect = rect.adjusted(innerPadding, innerPadding, -innerPadding, -innerPadding);

    QLinearGradient innerBorderGrad(0, innerRect.top(), 0, innerRect.bottom());
    innerBorderGrad.setColorAt(0.0, QColor("#76cbff"));
    innerBorderGrad.setColorAt(1.0, QColor("#0e4788"));

    QPen innerPen(QPen(QBrush(innerBorderGrad), DPR_REAL(1.5 * scale, dpr)));
    painter.setPen(innerPen);
    painter.drawRoundedRect(innerRect, DPR_INT(8 * scale, dpr, 0), DPR_INT(8 * scale, dpr, 0));
}

void LoadingDialog::setupUi() {
    const auto &ss       = ScreenScale::instance();
    const qreal dpr      = ss.dpr();
    const double scale   = ss.scale();
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(DPR_INT(35 * scale, dpr, 0), DPR_INT(28 * scale, dpr, 0), DPR_INT(35 * scale, dpr, 0), DPR_INT(22 * scale, dpr, 0));
    mainLayout->setSpacing(DPR_INT(16 * scale, dpr, 0));

    // -------------- 1. 标题区 --------------
    QHBoxLayout *titleLayout = new QHBoxLayout();
    titleLayout->setAlignment(Qt::AlignCenter);

    QPixmap logPix(":/title/title_log.png");
    logPix.setDevicePixelRatio(dpr);
    QLabel *logo = new QLabel(this);
    logo->setPixmap(logPix);

    QLabel *titleLabel = new QLabel(QStringLiteral("智能频谱监测仪"), this);
    titleLabel->setStyleSheet(QString(u8"color: #FFFFFF; font-size: %1px; font-weight: bold; font-family: '黑体';").arg(DPR_INT(22 * scale, dpr, 0)));

    titleLayout->addWidget(logo);
    titleLayout->addSpacing(DPR_INT(2 * scale, dpr, 0));
    titleLayout->addWidget(titleLabel);
    mainLayout->addLayout(titleLayout);

    // -------------- 2. 副标题长装饰线 --------------
    SubtitleDecorationWidget *subDeco = new SubtitleDecorationWidget(QStringLiteral("正在启动系统..."), this);
    mainLayout->addWidget(subDeco);

    // -------------- 3. 高科技渐变进度条包裹框 --------------
    mainLayout->addWidget(createProgressBarBox());

    // -------------- 4. 加载项列表 --------------
    mainLayout->addWidget(createTaskListWidget());

    mainLayout->addStretch();

    // -------------- 5. 底部 Footer --------------
    QHBoxLayout *footerLayout = new QHBoxLayout();

    QHBoxLayout *verLayout = new QHBoxLayout();
    verLayout->setSpacing(8);
    verLayout->addWidget(new HexIconWidget(this));

    QLabel *verLabel = new QLabel("Version 1.1.0   |   Build 2026.08.19", this);
    verLabel->setStyleSheet(u8"color: #c7cfdc; font-family: '黑体';");
    QFont font(u8"黑体", DPR_INT(6 * scale, dpr, 0));
    verLabel->setFont(font);
    verLayout->addWidget(verLabel);

    // 右下角精简小字号状态文本
    GradientStatusWidget *gradStatus = new GradientStatusWidget(QStringLiteral("正在检查运行环境"), this);

    footerLayout->addLayout(verLayout);
    footerLayout->addStretch();
    footerLayout->addWidget(gradStatus);

    mainLayout->addLayout(footerLayout);
}

QWidget* LoadingDialog::createProgressBarBox() {
    const auto &ss       = ScreenScale::instance();
    const qreal dpr      = ss.dpr();
    const double scale   = ss.scale();
    QWidget *box = new QWidget(this);
    box->setStyleSheet(R"(
        QWidget#ProgressBox {
            background-color: qlineargradient(
                x1:0, y1:0, x2:0, y2:1,
                stop:0 #02132f,
                stop:1 #021127
            );
            border: 1px solid #103357;
            border-radius: 6px;
        }
    )");
    box->setObjectName("ProgressBox");

    QHBoxLayout *layout = new QHBoxLayout(box);
    int leftRightPadding = DPR_INT(12 * scale, dpr, 0);
    int topBottomPadding = DPR_INT(8 * scale, dpr, 0);
    layout->setContentsMargins(leftRightPadding, topBottomPadding, leftRightPadding, topBottomPadding);
    layout->setSpacing(DPR_INT(14 * scale, dpr, 0));

    m_progressBar = new GlowProgressBar(box);
    m_progressBar->setValue(0);

    m_percentLabel = new QLabel("0%", box);
    m_percentLabel->setStyleSheet("color: #0084FF;");
    QFont font(u8"黑体", DPR_INT(11 * scale, dpr, 0), QFont::Bold);
    m_percentLabel->setFont(font);

    layout->addWidget(m_progressBar, 1);
    layout->addWidget(m_percentLabel);

    return box;
}

QWidget* LoadingDialog::createTaskListWidget() {
    QWidget *container = new QWidget(this);
    container->setStyleSheet(R"(
        QWidget#TaskListContainer {
            background-color: rgba(3, 11, 22, 0.6);
            border: 1px solid #0E2A4A;
            border-radius: 6px;
        }
    )");
    container->setObjectName("TaskListContainer");

    QVBoxLayout *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto createLine = [&]()-> QFrame*
    {
        QFrame* line = new QFrame(container);
        line->setFrameShape(QFrame::HLine);
        line->setStyleSheet("background-color: #0A1C30; border: none; max-height: 1px;");
        return line;
    };

    layout->addWidget(createStepRowWidget(&m_iconSystem, &m_nameLabelSystem, &m_statusLabelSystem, QStringLiteral("初始化系统环境"), QStringLiteral("等待中")));
    layout->addWidget(createLine());
    layout->addWidget(createStepRowWidget(&m_iconSpectrum, &m_nameLabelSpectrum, &m_statusLabelSpectrum, QStringLiteral("初始化频谱采集功能"), QStringLiteral("等待中")));
    layout->addWidget(createLine());
    layout->addWidget(createStepRowWidget(&m_iconSignal, &m_nameLabelSignal, &m_statusLabelSignal, QStringLiteral("初始化信号检测功能"), QStringLiteral("等待中")));
    layout->addWidget(createLine());
    layout->addWidget(createStepRowWidget(&m_iconAlert, &m_nameLabelAlert, &m_statusLabelAlert, QStringLiteral("初始化告警与数据管理功能"), QStringLiteral("等待中")));
    layout->addWidget(createLine());
    layout->addWidget(createStepRowWidget(&m_iconRunning, &m_nameLabelRunning, &m_statusLabelRunning, QStringLiteral("检查系统运行状态"), QStringLiteral("等待中")));
    return container;
}

QWidget* LoadingDialog::createStepRowWidget(TaskIconWidget **icon, QLabel **nameLabel, QLabel **statusLabel, const QString &name, const QString &statusText) {
    const auto &ss       = ScreenScale::instance();
    const qreal dpr      = ss.dpr();
    const double scale   = ss.scale();

    QWidget *rowWidget = new QWidget(this);
    QHBoxLayout *row = new QHBoxLayout(rowWidget);
    row->setContentsMargins(16, 9, 16, 9);

    *icon = new TaskIconWidget(TaskIconWidget::Pending, rowWidget);

    *nameLabel = new QLabel(name, rowWidget);
    QString nameColor = "#4A6785";
    (*nameLabel)->setStyleSheet(QString("color: %1; font-size: %2px; font-family: 'Microsoft YaHei';").arg(nameColor).arg(DPR_INT(13 * scale, dpr, 0)));

    *statusLabel = new QLabel(statusText, rowWidget);
    QString statusColor = "#4A6785";
    (*statusLabel)->setStyleSheet(QString("color: %1; font-size: %2px; font-family: 'Microsoft YaHei';").arg(statusColor).arg(DPR_INT(13 * scale, dpr, 0)));

    row->addWidget(*icon);
    row->addSpacing(DPR_INT(12 * scale, dpr, 0));
    row->addWidget(*nameLabel);
    row->addStretch();
    row->addWidget(*statusLabel);

    return rowWidget;
}

void LoadingDialog::updateStepRowWidget(TaskIconWidget* icon, QLabel* nameLabel, QLabel* statusLabel, TaskIconWidget::State state)
{
    const auto &ss       = ScreenScale::instance();
    const qreal dpr      = ss.dpr();
    const double scale   = ss.scale();
    if (state == TaskIconWidget::InProgress)
    {
        icon->setState(state);
        QString nameColor = "#C8DCF0";
        nameLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-family: 'Microsoft YaHei';").arg(nameColor).arg(DPR_INT(13 * scale, dpr, 0)));
        QString statusColor = "#0084FF";
        statusLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-family: 'Microsoft YaHei';").arg(statusColor).arg(DPR_INT(13 * scale, dpr, 0)));
        statusLabel->setText(QStringLiteral("进行中"));
    }
    else if (state == TaskIconWidget::Completed)
    {
        icon->setState(state);
        QString nameColor = "#C8DCF0";
        nameLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-family: 'Microsoft YaHei';").arg(nameColor).arg(DPR_INT(13 * scale, dpr, 0)));
        QString statusColor = "#0084FF";
        statusLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-family: 'Microsoft YaHei';").arg(statusColor).arg(DPR_INT(13 * scale, dpr, 0)));
        statusLabel->setText(QStringLiteral("已完成"));
    }
    else if (state == TaskIconWidget::Failed)
    {
        icon->setState(state);
        QString nameColor = "#C8DCF0";
        nameLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-family: 'Microsoft YaHei';").arg(nameColor).arg(DPR_INT(13 * scale, dpr, 0)));
        QString statusColor = "#E74C3C";
        statusLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-family: 'Microsoft YaHei';").arg(statusColor).arg(DPR_INT(13 * scale, dpr, 0)));
        statusLabel->setText(QStringLiteral("失败"));
    }
}
