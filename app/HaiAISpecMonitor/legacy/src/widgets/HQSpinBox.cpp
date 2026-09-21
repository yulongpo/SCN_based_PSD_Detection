#include "HQSpinBox.h"

#include "comm/CommonMacros.h"
#include "comm/FontManager.h"
#include "comm/ScreenScale.h"
#include "comm/ThemeManager.h"

#include <QApplication>
#include <QDoubleValidator>
#include <QEvent>
#include <QIntValidator>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>

namespace {

QString rgbaColor(const QColor &color, const QString &fallback)
{
    if (!color.isValid()) {
        return fallback;
    }
    return QString("rgba(%1,%2,%3,%4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(color.alpha());
}

} // namespace

HQSpinBox::HQSpinBox(QWidget *parent)
    : QWidget(parent)
    , m_lineEdit(new QLineEdit(this))
    , m_emphasisAnimation(new QPropertyAnimation(this, "emphasisProgress", this))
    , m_autoRepeatTimer(new QTimer(this))
    , m_value(0.0)
    , m_min(0.0)
    , m_max(99.0)
    , m_decimals(0)
    , m_step(1.0)
    , m_emphasisProgress(0.0)
    , m_hovered(false)
    , m_focused(false)
    , m_pressedArrow(0)
{
    setObjectName("hqSpinBox");
    setAttribute(Qt::WA_Hover, true);
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setFocusProxy(m_lineEdit);

    // 内部输入框设置
    m_lineEdit->setObjectName("hqSpinBoxInner");
    m_lineEdit->setFrame(false);
    // 禁用右键弹出菜单（复制/粘贴/剪切等），由上层业务决定是否需要这些功能
    m_lineEdit->setContextMenuPolicy(Qt::NoContextMenu);
    m_lineEdit->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_lineEdit->installEventFilter(this);

    // 长按自动递增/递减：首次延迟 500ms，之后每 80ms 触发一次
    m_autoRepeatTimer->setSingleShot(false);
    m_autoRepeatTimer->setInterval(80);
    connect(m_autoRepeatTimer, &QTimer::timeout, this, &HQSpinBox::onAutoRepeat);

    m_emphasisAnimation->setDuration(180);
    m_emphasisAnimation->setEasingCurve(QEasingCurve::OutCubic);

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &HQSpinBox::applyThemeAppearance);

    // 用户在输入框中编辑后同步值
    connect(m_lineEdit, &QLineEdit::editingFinished, this, [this]() {
        bool ok = false;
        double v = m_lineEdit->text().toDouble(&ok);
        if (ok) {
            setValue(v);
        } else {
            // 输入非法时恢复为当前值
            m_lineEdit->setText(valueToText());
        }
    });

    setRange(m_min, m_max);
    setDecimals(m_decimals);
    setValue(m_value);

    refreshMetrics();
    applyThemeAppearance(ThemeManager::instance().isNightMode());
    qApp->installEventFilter(this);
}

HQSpinBox::~HQSpinBox()
{
    // 析构时如有残留的全局光标覆盖，恢复默认
    if (m_cursorOverridden) {
        QApplication::restoreOverrideCursor();
        m_cursorOverridden = false;
    }
    qApp->removeEventFilter(this);
}

void HQSpinBox::setValue(double value)
{
    value = qBound(m_min, value, m_max);
    if (qFuzzyCompare(m_value, value)) {
        return;
    }
    m_value = value;
    m_lineEdit->setText(valueToText());
    emit valueChanged(m_value);
}

void HQSpinBox::setRange(double min, double max)
{
    if (min > max) {
        qSwap(min, max);
    }
    m_min = min;
    m_max = max;
    m_value = qBound(m_min, m_value, m_max);
    m_lineEdit->setText(valueToText());
}

void HQSpinBox::setDecimals(int decimals)
{
    m_decimals = qMax(0, decimals);

    // 根据小数位数选择整数或浮点验证器，并同步 step 精度
    if (m_decimals == 0) {
        auto *validator = new QIntValidator(static_cast<int>(m_min),
                                            static_cast<int>(m_max), this);
        m_lineEdit->setValidator(validator);
    } else {
        auto *validator = new QDoubleValidator(m_min, m_max, m_decimals, this);
        validator->setNotation(QDoubleValidator::StandardNotation);
        m_lineEdit->setValidator(validator);
    }
    m_lineEdit->setText(valueToText());
}

void HQSpinBox::setSingleStep(double step)
{
    m_step = qMax(0.0, step);
}

QSize HQSpinBox::sizeHint() const
{
    return QSize(scaledPx(110, 60),
                 scaledPx(36, 24));
}

QString HQSpinBox::valueToText() const
{
    if (m_decimals == 0) {
        return QString::number(static_cast<int>(m_value));
    }
    return QString::number(m_value, 'f', m_decimals);
}

int HQSpinBox::arrowAtPos(const QPoint &pos) const
{
    const int arrowZoneLeft = width() - scaledPx(32, 18);
    if (pos.x() < arrowZoneLeft) {
        return 0; // 在输入区域内
    }
    const int midY = height() / 2;
    if (pos.y() < midY) {
        return 1;  // 上箭头 → direction=1 → stepBy(+1) → 增加值
    }
    return -1; // 下箭头 → direction=-1 → stepBy(-1) → 减少值
}

QRectF HQSpinBox::upArrowRect() const
{
    const int arrowZoneLeft = width() - scaledPx(32, 18);
    return QRectF(arrowZoneLeft, 0,
                  width() - arrowZoneLeft, height() / 2.0);
}

QRectF HQSpinBox::downArrowRect() const
{
    const int arrowZoneLeft = width() - scaledPx(32, 18);
    return QRectF(arrowZoneLeft, height() / 2.0,
                  width() - arrowZoneLeft, height() / 2.0);
}

void HQSpinBox::stepBy(int direction)
{
    double newValue = m_value + direction * m_step;
    // 对于整数模式，确保取整
    if (m_decimals == 0) {
        newValue = qRound(newValue);
    }
    setValue(newValue);
}

// --------------- 事件处理 ---------------

void HQSpinBox::enterEvent(QEvent *event)
{
    m_hovered = true;
    animateEmphasisTo(targetEmphasis());
    QWidget::enterEvent(event);
}

void HQSpinBox::leaveEvent(QEvent *event)
{
    m_hovered = false;
    m_autoRepeatTimer->stop();
    m_pressedArrow = 0;
    animateEmphasisTo(targetEmphasis());
    QWidget::leaveEvent(event);
}

void HQSpinBox::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    m_pressedArrow = arrowAtPos(event->pos());
    if (m_pressedArrow != 0) {
        stepBy(m_pressedArrow);
        // 长按 500ms 后启动自动递增/递减
        QTimer::singleShot(500, this, [this]() {
            if (m_pressedArrow != 0) {
                m_autoRepeatTimer->start();
            }
        });
        event->accept();
        return;
    }

    QWidget::mousePressEvent(event);
}

void HQSpinBox::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_autoRepeatTimer->stop();
        m_pressedArrow = 0;
        update();
    }
    QWidget::mouseReleaseEvent(event);
}

void HQSpinBox::onAutoRepeat()
{
    if (m_pressedArrow != 0) {
        stepBy(m_pressedArrow);
    } else {
        m_autoRepeatTimer->stop();
    }
}

bool HQSpinBox::eventFilter(QObject *obj, QEvent *event)
{
    // 内部 QLineEdit 焦点变化 → 更新自绘外观
    if (obj == m_lineEdit) {
        if (event->type() == QEvent::FocusIn) {
            m_focused = true;
            animateEmphasisTo(targetEmphasis());
        } else if (event->type() == QEvent::FocusOut) {
            m_focused = false;
            animateEmphasisTo(targetEmphasis());
        }
    }

    // 点击控件外部时清除焦点
    if (event->type() == QEvent::MouseButtonPress
        || event->type() == QEvent::NonClientAreaMouseButtonPress) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        const QPoint localPos = mapFromGlobal(mouseEvent->globalPos());
        if (!rect().contains(localPos) && m_lineEdit->hasFocus()) {
            m_lineEdit->clearFocus();
        }
    }

    if (event->type() == QEvent::MouseMove) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        const bool inRect = isVisible() && rect().contains(mapFromGlobal(mouseEvent->globalPos()));
        if (!isEnabled() && inRect) {
            if (!m_cursorOverridden) {
                QApplication::setOverrideCursor(Qt::ForbiddenCursor);
                m_cursorOverridden = true;
            }
        } else if (m_cursorOverridden) {
            QApplication::restoreOverrideCursor();
            m_cursorOverridden = false;
        }
    }

    return QWidget::eventFilter(obj, event);
}

// --------------- 绘制 ---------------

void HQSpinBox::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const int borderWidth = scaledPx(1, 1);
    const int radius = scaledPx(10, 4);

    // 复用 HQComboBox 的主题色
    const QColor baseBg = themeColor("backgroundColor", QColor(255, 255, 255));
    const QColor hoverBg = themeColor("hoverBackgroundColor", baseBg);
    const QColor focusBg = themeColor("activeBackgroundColor", hoverBg);
    const QColor borderBase = themeColor("borderColor", QColor(220, 223, 230));
    const QColor borderHover = themeColor("hoverBorderColor", QColor(192, 196, 204));
    const QColor borderFocus = themeColor("activeBorderColor", QColor(64, 158, 255));
    const QColor shadowColor = themeColor("activeShadowColor", QColor(64, 158, 255, 38));

    QColor bg = baseBg;
    QColor border = borderBase;
    if (m_focused) {
        bg = focusBg;
        border = borderFocus;
    } else if (m_hovered) {
        bg = hoverBg;
        border = borderHover;
    }

    QColor mixedBg = QColor::fromRgbF(
        baseBg.redF() + (bg.redF() - baseBg.redF()) * m_emphasisProgress,
        baseBg.greenF() + (bg.greenF() - baseBg.greenF()) * m_emphasisProgress,
        baseBg.blueF() + (bg.blueF() - baseBg.blueF()) * m_emphasisProgress,
        baseBg.alphaF() + (bg.alphaF() - baseBg.alphaF()) * m_emphasisProgress);
    QColor mixedBorder = QColor::fromRgbF(
        borderBase.redF() + (border.redF() - borderBase.redF()) * m_emphasisProgress,
        borderBase.greenF() + (border.greenF() - borderBase.greenF()) * m_emphasisProgress,
        borderBase.blueF() + (border.blueF() - borderBase.blueF()) * m_emphasisProgress,
        borderBase.alphaF() + (border.alphaF() - borderBase.alphaF()) * m_emphasisProgress);

    const QRectF box(0.5, 0.5, width() - 1.0, height() - 1.0);
    QPainterPath path;
    path.addRoundedRect(box, radius, radius);

    // 聚焦阴影
    if (shadowColor.isValid() && m_emphasisProgress > 0.0) {
        QColor outer = shadowColor;
        outer.setAlphaF(shadowColor.alphaF() * m_emphasisProgress * 0.8);
        painter.setPen(Qt::NoPen);
        painter.setBrush(outer);
        painter.drawRoundedRect(box.adjusted(-0.5, -0.5, 0.5, 0.5), radius + 0.5, radius + 0.5);
    }

    // 填充背景
    painter.fillPath(path, mixedBg);

    // 边框
    QPen borderPen(mixedBorder, borderWidth);
    borderPen.setCosmetic(true);
    painter.setPen(borderPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);

    // 箭头颜色
    const QColor arrowColor = themeColor("arrowColor", QColor(96, 98, 102));
    const QColor arrowActiveColor = themeColor("arrowActiveColor", QColor(64, 158, 255));
    QColor mixedArrow = QColor::fromRgbF(
        arrowColor.redF() + (arrowActiveColor.redF() - arrowColor.redF()) * m_emphasisProgress,
        arrowColor.greenF() + (arrowActiveColor.greenF() - arrowColor.greenF()) * m_emphasisProgress,
        arrowColor.blueF() + (arrowActiveColor.blueF() - arrowColor.blueF()) * m_emphasisProgress,
        arrowColor.alphaF() + (arrowActiveColor.alphaF() - arrowColor.alphaF()) * m_emphasisProgress);

    const int arrowSize = scaledPx(8, 4);

    // 绘制上箭头 ▲ （增大增加值）
    {
        const QRectF upRect = upArrowRect();
        const qreal cx = upRect.center().x();
        const qreal cy = upRect.center().y();
        QPainterPath arrowPath;
        arrowPath.moveTo(cx, cy - arrowSize / 2.0);
        arrowPath.lineTo(cx + arrowSize / 2.0, cy + arrowSize / 2.0);
        arrowPath.lineTo(cx - arrowSize / 2.0, cy + arrowSize / 2.0);
        arrowPath.closeSubpath();
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_pressedArrow == 1 ? arrowActiveColor : mixedArrow);
        painter.drawPath(arrowPath);
    }

    // 绘制下箭头 ▼ （减少值）
    {
        const QRectF downRect = downArrowRect();
        const qreal cx = downRect.center().x();
        const qreal cy = downRect.center().y();
        QPainterPath arrowPath;
        arrowPath.moveTo(cx - arrowSize / 2.0, cy - arrowSize / 2.0);
        arrowPath.lineTo(cx + arrowSize / 2.0, cy - arrowSize / 2.0);
        arrowPath.lineTo(cx, cy + arrowSize / 2.0);
        arrowPath.closeSubpath();
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_pressedArrow == -1 ? arrowActiveColor : mixedArrow);
        painter.drawPath(arrowPath);
    }
}

void HQSpinBox::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);

    const int leftPadding = scaledPx(12, 6);
    const int arrowZoneWidth = scaledPx(32, 18);
    m_lineEdit->setGeometry(leftPadding, 0,
                            qMax(10, width() - arrowZoneWidth - leftPadding),
                            height());
}

void HQSpinBox::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::EnabledChange) {
        if (isEnabled()) {
            setCursor(Qt::ArrowCursor);
            m_lineEdit->setCursor(Qt::ArrowCursor);
            // 恢复时清除全局光标覆盖
            if (m_cursorOverridden) {
                QApplication::restoreOverrideCursor();
                m_cursorOverridden = false;
            }
        } else {
            setCursor(Qt::ForbiddenCursor);
            m_lineEdit->setCursor(Qt::ForbiddenCursor);
        }
    }
    QWidget::changeEvent(event);
}

// --------------- 主题与动画 ---------------

void HQSpinBox::applyThemeAppearance(bool night)
{
    Q_UNUSED(night);
    QFont font = FontManager::instance().font(scaledPx(14, 10), QFont::Normal);
    m_lineEdit->setFont(font);
    const QColor placeholderColor = themeColor("placeholderColor", QColor(192, 196, 204));
    m_lineEdit->setStyleSheet(
        QString("QLineEdit { background: transparent; border: none; padding: 0; color: %1; }"
                "QLineEdit::placeholder { color: %2; }")
            .arg(rgbaColor(themeColor("textColor", QColor(48, 49, 51)), QStringLiteral("#303133")),
                 rgbaColor(placeholderColor, QStringLiteral("#c0c4cc"))));
    update();
}

void HQSpinBox::setEmphasisProgress(qreal progress)
{
    progress = qBound(0.0, progress, 1.0);
    if (qFuzzyCompare(m_emphasisProgress, progress)) {
        return;
    }
    m_emphasisProgress = progress;
    update();
}

QColor HQSpinBox::themeColor(const QString &key, const QColor &fallback) const
{
    const QColor color = ThemeManager::instance().color(QStringLiteral("hqComboBox.") + key);
    return color.isValid() ? color : fallback;
}

void HQSpinBox::animateEmphasisTo(qreal value)
{
    value = qBound(0.0, value, 1.0);
    m_emphasisAnimation->stop();
    m_emphasisAnimation->setStartValue(m_emphasisProgress);
    m_emphasisAnimation->setEndValue(value);
    m_emphasisAnimation->start();
}

qreal HQSpinBox::targetEmphasis() const
{
    if (m_focused) {
        return 1.0;
    }
    return m_hovered ? 0.65 : 0.0;
}

void HQSpinBox::refreshMetrics()
{
    setFixedSize(sizeHint());
}

int HQSpinBox::scaledPx(int designPx, int min) const
{
    const auto &ss = ScreenScale::instance();
    return DPR_INT(designPx * ss.scale(), ss.dpr(), min);
}
