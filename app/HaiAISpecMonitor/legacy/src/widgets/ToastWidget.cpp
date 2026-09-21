#include "ToastWidget.h"
#include "comm/ScreenScale.h"
#include "comm/CommonMacros.h"
#include "comm/ThemeManager.h"
#include <QHBoxLayout>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include <QPainter>
#include <QResizeEvent>
#include <QShowEvent>

// ============================================================
// ToastWidget 单例指针
// ============================================================
ToastWidget *ToastWidget::s_instance = nullptr;

// ============================================================
// ToastItem 实现
// ============================================================

ToastItem::ToastItem(QWidget *parent, Type type, const QString &text, int durationMs)
    : QFrame(parent)
    , m_type(type)
{
    // 设置为独立的顶层工具窗口，确保悬浮在 Dialog 等所有窗口之上
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    // 显示时不抢焦点，避免干扰当前操作的 Dialog
    setAttribute(Qt::WA_ShowWithoutActivating, true);

    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

    const int iconSize  = DPR_INT(22 * scale, dpr, 0);
    const int pad       = DPR_INT(12 * scale, dpr, 0);
    const int spacing   = DPR_INT(8 * scale, dpr, 0);
    const int toastW    = DPR_INT(340 * scale, dpr, 0);

    // 关闭按钮
    m_closeBtn = new QPushButton(QStringLiteral("✕"), this);
    m_closeBtn->setFixedSize(iconSize, iconSize);
    m_closeBtn->setFlat(true);
    m_closeBtn->setCursor(Qt::PointingHandCursor);

    // 图标（程序化生成）
    m_iconLabel = new QLabel(this);
    m_iconLabel->setPixmap(generateIcon(type));
    m_iconLabel->setFixedSize(iconSize, iconSize);

    // 文字
    m_textLabel = new QLabel(text, this);
    m_textLabel->setWordWrap(true);
    {
        QFont f;
        f.setPixelSize(DPR_INT(13 * scale, dpr, 0));
        m_textLabel->setFont(f);
    }

    // 布局
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(pad, pad, pad, pad);
    layout->setSpacing(spacing);
    layout->addWidget(m_iconLabel);
    layout->addWidget(m_textLabel, 1);
    layout->addWidget(m_closeBtn);

    // 样式 — 根据类型设置不同颜色（从主题配置读取）
    const auto &tm = ThemeManager::instance();
    const QString typeSection = (type == Info)  ? QStringLiteral("toast.info") :
                                (type == Warning) ? QStringLiteral("toast.warning") :
                                                    QStringLiteral("toast.error");
    const QString typeColor = tm.colorString(typeSection + ".accentColor");
    const QString typeBg    = tm.colorString(typeSection + ".backgroundColor");
    const QString textColor = tm.colorString("toast.textColor");
    const QString closeColor     = tm.colorString("toast.closeButtonColor");
    const QString closeHoverColor = tm.colorString("toast.closeButtonHoverColor");

    setStyleSheet(QString(R"(
        ToastItem {
            background-color: %1;
            border: 1px solid %2;
            border-radius: 8px;
        }
        QLabel {
            color: %3;
            background: transparent;
            border: none;
        }
        QPushButton {
            color: %4;
            background: transparent;
            border: none;
            font-size: 14px;
        }
        QPushButton:hover {
            color: %5;
        }
    )").arg(typeBg, typeColor, textColor, closeColor, closeHoverColor));

    // 固定宽度，高度自适应
    setFixedWidth(toastW);
    adjustSize();

    // 自动关闭定时器
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &ToastItem::onTimeout);
    if (durationMs > 0)
        m_timer->start(durationMs);

    connect(m_closeBtn, &QPushButton::clicked, this, &ToastItem::closeAnimated);

    hide();
}

QPixmap ToastItem::generateIcon(Type type) const
{
    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

    const int s = DPR_INT(22 * scale, dpr, 0);
    QPixmap pix(s, s);
    pix.fill(Qt::transparent);
    pix.setDevicePixelRatio(dpr);

    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);

    QColor bgColor;
    QString symbol;
    {
        const auto &tm = ThemeManager::instance();
        const QString typeSection = (type == Info)  ? QStringLiteral("toast.info") :
                                    (type == Warning) ? QStringLiteral("toast.warning") :
                                                        QStringLiteral("toast.error");
        bgColor = tm.color(typeSection + ".accentColor");
    }
    switch (type) {
    case Info:    symbol = QStringLiteral("i");    break;
    case Warning: symbol = QStringLiteral("!");    break;
    case Error:   symbol = QStringLiteral("✕");   break;
    }

    // 圆底
    int r = s / 2;
    p.setBrush(bgColor);
    p.setPen(Qt::NoPen);
    p.drawEllipse(QPointF(r, r), r - 1, r - 1);

    // 符号
    p.setPen(Qt::white);
    QFont f;
    f.setPixelSize(s * 0.55);
    f.setBold(true);
    p.setFont(f);
    p.drawText(QRect(0, 0, s, s), Qt::AlignCenter, symbol);

    p.end();
    return pix;
}

void ToastItem::slideIn()
{
    const int offsetX = width() + 50;

    // 初始位置在右侧外
    move(pos() + QPoint(offsetX, 0));

    QPropertyAnimation *anim = new QPropertyAnimation(this, "pos", this);
    anim->setDuration(250);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    anim->setStartValue(pos());
    anim->setEndValue(pos() - QPoint(offsetX, 0));
    anim->start(QAbstractAnimation::DeleteWhenStopped);

    show();
    raise();  // 确保显示在最顶层
}

void ToastItem::showEvent(QShowEvent * /*event*/)
{
    // 每次显示时自动提升到最顶层，防止被其他控件遮挡
    raise();
}

void ToastItem::closeAnimated()
{
    m_timer->stop();

    auto *effect = new QGraphicsOpacityEffect(this);
    setGraphicsEffect(effect);

    auto *anim = new QPropertyAnimation(effect, "opacity", this);
    anim->setDuration(200);
    anim->setStartValue(1.0);
    anim->setEndValue(0.0);
    connect(anim, &QPropertyAnimation::finished, this, [this]() {
        emit closed(this);
        deleteLater();
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void ToastItem::onTimeout()
{
    closeAnimated();
}


// ============================================================
// ToastWidget 实现
// ============================================================

ToastWidget *ToastWidget::instance(QWidget *parentWindow)
{
    if (!s_instance && parentWindow)
        s_instance = new ToastWidget(parentWindow);
    return s_instance;
}

ToastWidget::ToastWidget(QWidget *parentWindow)
    : QObject(parentWindow)
    , m_parentWindow(parentWindow)
{
    // 监听父窗口大小变化，以便重新定位 Toast
    if (m_parentWindow)
        m_parentWindow->installEventFilter(this);
}

void ToastWidget::showInfo(const QString &text, int durationMs)
{
    show(ToastItem::Info, text, durationMs);
}

void ToastWidget::showWarning(const QString &text, int durationMs)
{
    show(ToastItem::Warning, text, durationMs);
}

void ToastWidget::showError(const QString &text, int durationMs)
{
    show(ToastItem::Error, text, durationMs);
}

void ToastWidget::show(ToastItem::Type type, const QString &text, int durationMs)
{
    if (!m_parentWindow) return;

    auto *item = new ToastItem(m_parentWindow, type, text, durationMs);
    connect(item, &ToastItem::closed, this, &ToastWidget::onToastClosed);

    m_toasts.append(item);
    repositionAll();    // 计算所有 toast 位置
    item->slideIn();    // 新 toast 滑入
}

void ToastWidget::repositionAll()
{
    if (!m_parentWindow || m_toasts.isEmpty()) return;

    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

    const int toastW   = DPR_INT(340 * scale, dpr, 0);
    const int marginR  = DPR_INT(20 * scale, dpr, 0);
    const int marginB  = DPR_INT(20 * scale, dpr, 0);
    const int spacing  = DPR_INT(8 * scale, dpr, 0);

    const int pw = m_parentWindow->width();
    const int ph = m_parentWindow->height();
    // 先计算父窗口内的相对坐标（右下角）
    const int localX = pw - marginR - toastW;
    const int localBottomY = ph - marginB;

    // 从底部向上累加排列，每个 toast 按自身实际高度计算位置
    int currentY = localBottomY;
    for (int i = m_toasts.size() - 1; i >= 0; --i) {
        ToastItem *item = m_toasts[i];
        const int itemH = item->height();

        currentY -= itemH;                     // 当前 toast 的顶部位置
        const int localY = currentY;
        currentY -= spacing;                   // 留出间距给上方的 toast

        // ToastItem 是顶层工具窗口，需要全局坐标定位
        const QPoint targetPos = m_parentWindow->mapToGlobal(QPoint(localX, localY));

        // 确保每次重排时 toast 都在最顶层
        item->raise();

        if (item->pos() == targetPos)
            continue;

        if (!item->isVisible()) {
            item->move(targetPos);
        } else {
            QPropertyAnimation *anim = new QPropertyAnimation(item, "pos", item);
            anim->setDuration(200);
            anim->setEasingCurve(QEasingCurve::OutCubic);
            anim->setStartValue(item->pos());
            anim->setEndValue(targetPos);
            anim->start(QAbstractAnimation::DeleteWhenStopped);
        }
    }
}

void ToastWidget::onToastClosed(ToastItem *item)
{
    m_toasts.removeOne(item);
    repositionAll();
}

bool ToastWidget::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_parentWindow && event->type() == QEvent::Resize) {
        repositionAll();
    }
    return QObject::eventFilter(obj, event);
}
