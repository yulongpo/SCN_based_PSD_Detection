#include "HQLineEdit.h"

#include "comm/CommonMacros.h"
#include "comm/FontManager.h"
#include "comm/ScreenScale.h"
#include "comm/ThemeManager.h"

#include <QApplication>
#include <QEvent>
#include <QLineEdit>
#include <QDoubleValidator>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>

namespace {

/** 将 QColor 转换为 CSS rgba() 字符串，无效颜色返回 fallback */
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

HQLineEdit::HQLineEdit(QWidget *parent)
    : QWidget(parent)
    , m_lineEdit(new QLineEdit(this))
    , m_emphasisAnimation(new QPropertyAnimation(this, "emphasisProgress", this))
    , m_emphasisProgress(0.0)
    , m_hovered(false)
    , m_focused(false)
{
    setObjectName("hqLineEdit");
    setAttribute(Qt::WA_Hover, true);
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setFocusProxy(m_lineEdit); // 将焦点代理到内部 QLineEdit，点击外部时自动失去焦点

    // 内部输入框：透明背景、无边框，外观完全由外层 paintEvent 绘制
    m_lineEdit->setObjectName("hqLineEditInner");
    m_lineEdit->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    m_lineEdit->setFrame(false);
    // 禁用右键弹出菜单（复制/粘贴/剪切等），由上层业务决定是否需要这些功能
    m_lineEdit->setContextMenuPolicy(Qt::NoContextMenu);
    m_lineEdit->setStyleSheet(
        QStringLiteral("QLineEdit { background: transparent; border: none; padding: 0; }"));

    // 监听内部输入框的焦点变化
    m_lineEdit->installEventFilter(this);

    m_emphasisAnimation->setDuration(180);
    m_emphasisAnimation->setEasingCurve(QEasingCurve::OutCubic);

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &HQLineEdit::applyThemeAppearance);
    connect(m_lineEdit, &QLineEdit::textChanged,
            this, &HQLineEdit::textChanged);
    connect(m_lineEdit, &QLineEdit::editingFinished,
            this, &HQLineEdit::editingFinished);
    connect(m_lineEdit, &QLineEdit::editingFinished,
            this, &HQLineEdit::formatDecimalPlaces);

    refreshMetrics();
    updateAppearance();
    applyThemeAppearance(ThemeManager::instance().isNightMode());
    qApp->installEventFilter(this);
}

HQLineEdit::~HQLineEdit()
{
    // 析构时如有残留的全局光标覆盖，恢复默认
    if (m_cursorOverridden) {
        QApplication::restoreOverrideCursor();
        m_cursorOverridden = false;
    }
    qApp->removeEventFilter(this);
}

QString HQLineEdit::text() const
{
    return m_lineEdit->text();
}

void HQLineEdit::setText(const QString &text)
{
    m_lineEdit->setText(text);
}

QString HQLineEdit::placeholderText() const
{
    return m_lineEdit->placeholderText();
}

void HQLineEdit::setPlaceholderText(const QString &text)
{
    m_lineEdit->setPlaceholderText(text);
}

void HQLineEdit::setUnit(const QString &unit)
{
    if (m_unit == unit) {
        return;
    }
    m_unit = unit;
    // 单位变化时调整内部输入框的右边界，使用实际文字宽度而非固定值
    const int leftPadding = scaledPx(12, 6);
    const int unitReserve = unitTextWidth();
    m_lineEdit->setGeometry(leftPadding, 0,
                            qMax(10, width() - unitReserve - leftPadding),
                            height());
    update();
}

void HQLineEdit::setDoubleRange(double min, double max, int decimals)
{
    m_decimals = decimals;  // 记录小数位数，供 formatDecimalPlaces 使用
    auto *validator = new QDoubleValidator(min, max, decimals, this);
    validator->setNotation(QDoubleValidator::StandardNotation);
    m_lineEdit->setValidator(validator);
}

void HQLineEdit::formatDecimalPlaces()
{
    // 未设置小数位数时跳过
    if (m_decimals <= 0) return;

    const QString text = m_lineEdit->text();
    // 空文本跳过（用户可能清空了输入框）
    if (text.isEmpty()) return;

    // 尝试转换为 double，失败则说明是非法中间态（如 "123."、"1e"），不处理
    bool ok = false;
    const double value = text.toDouble(&ok);
    if (!ok) return;

    // 格式化为固定小数位数
    const QString formatted = QString::number(value, 'f', m_decimals);
    if (formatted != text) {
        // 阻止 textChanged 信号导致不必要的联动更新
        m_lineEdit->blockSignals(true);
        m_lineEdit->setText(formatted);
        m_lineEdit->blockSignals(false);
    }
}

QSize HQLineEdit::sizeHint() const
{
    return QSize(scaledPx(140, 80),
                 scaledPx(36, 24));
}

void HQLineEdit::enterEvent(QEvent *event)
{
    m_hovered = true;
    animateEmphasisTo(targetEmphasis());
    QWidget::enterEvent(event);
}

void HQLineEdit::leaveEvent(QEvent *event)
{
    m_hovered = false;
    animateEmphasisTo(targetEmphasis());
    QWidget::leaveEvent(event);
}

void HQLineEdit::changeEvent(QEvent *event)
{
    // 禁用/启用状态切换时同步更新光标
    // 由于禁用后 Qt 不向控件分发鼠标事件，仅靠 setCursor 不够，
    // 还需配合 eventFilter 中 QApplication::setOverrideCursor 全局覆盖
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

void HQLineEdit::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const int borderWidth = scaledPx(1, 1);
    const int radius = scaledPx(10, 4);

    // 主题色，key 前缀 "hqLineEdit." 与 HQComboBox 的 "hqComboBox." 对称
    const QColor baseBg = themeColor("backgroundColor", QColor(255, 255, 255));
    const QColor hoverBg = themeColor("hoverBackgroundColor", baseBg);
    const QColor focusBg = themeColor("activeBackgroundColor", hoverBg);
    const QColor borderBase = themeColor("borderColor", QColor(220, 223, 230));
    const QColor borderHover = themeColor("hoverBorderColor", QColor(192, 196, 204));
    const QColor borderFocus = themeColor("activeBorderColor", QColor(64, 158, 255));
    const QColor shadowColor = themeColor("activeShadowColor", QColor(64, 158, 255, 38));

    // 根据状态选择目标色
    QColor bg = baseBg;
    QColor border = borderBase;
    if (m_focused) {
        bg = focusBg;
        border = borderFocus;
    } else if (m_hovered) {
        bg = hoverBg;
        border = borderHover;
    }

    // 使用 emphasisProgress 做平滑的颜色插值
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

    // 绘制边框（cosmetic 确保高 DPI 下仍是清晰 1px）
    QPen borderPen(mixedBorder, borderWidth);
    borderPen.setCosmetic(true);
    painter.setPen(borderPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);

    // 绘制右侧单位文字
    if (!m_unit.isEmpty()) {
        const QColor unitColor = themeColor("detailButtonTextColor", QColor(144, 147, 153));
        QFont unitFont = FontManager::instance().font(scaledPx(12, 9), QFont::Normal);
        painter.setFont(unitFont);
        painter.setPen(unitColor);

        const int rightGap = scaledPx(12, 6);
        const int unitWidth = scaledPx(40, 20);
        const QRectF unitRect(width() - rightGap - unitWidth, 0,
                              unitWidth, height());
        painter.drawText(unitRect, Qt::AlignRight | Qt::AlignVCenter, m_unit);
    }
}

void HQLineEdit::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);

    const int leftPadding = scaledPx(12, 6);
    const int unitReserve = unitTextWidth();
    m_lineEdit->setGeometry(leftPadding, 0,
                            qMax(10, width() - unitReserve - leftPadding),
                            height());
}

int HQLineEdit::unitTextWidth() const
{
    if (m_unit.isEmpty()) {
        return scaledPx(8, 4); // 无单位时只留最小边距
    }
    QFont unitFont = FontManager::instance().font(scaledPx(12, 9), QFont::Normal);
    QFontMetrics fm(unitFont);
    const int rightGap = scaledPx(12, 6);
    return fm.horizontalAdvance(m_unit) + rightGap;
}

bool HQLineEdit::eventFilter(QObject *obj, QEvent *event)
{
    // 处理内部 QLineEdit 的焦点变化（更新自绘外观）
    if (obj == m_lineEdit) {
        if (event->type() == QEvent::FocusIn) {
            m_focused = true;
            animateEmphasisTo(targetEmphasis());
        } else if (event->type() == QEvent::FocusOut) {
            m_focused = false;
            animateEmphasisTo(targetEmphasis());
        }
    }

    // 点击控件外部时清除焦点（处理客户区和非客户区两种鼠标事件，
    // 标题栏因 HTCAPTION 产生的是 NonClientArea 事件，不处理会导致焦点残留）
    if (event->type() == QEvent::MouseButtonPress
        || event->type() == QEvent::NonClientAreaMouseButtonPress) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        const QPoint localPos = mapFromGlobal(mouseEvent->globalPos());
        if (!rect().contains(localPos) && m_lineEdit->hasFocus()) {
            m_lineEdit->clearFocus();
        }
    }

    // 全局鼠标移动：禁用状态下鼠标移入控件区域时显示红色禁止光标
    // Qt 对 disabled 控件不派发鼠标事件，无法通过 setCursor() 改变光标，
    // 必须借助 QApplication::setOverrideCursor 做全局覆盖
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

void HQLineEdit::applyThemeAppearance(bool night)
{
    Q_UNUSED(night);
    QFont font = FontManager::instance().font(scaledPx(14, 10), QFont::Normal);
    m_lineEdit->setFont(font);
    // 占位文字颜色通过 QSS 设置以保证即时更新
    const QColor placeholderColor = themeColor("placeholderColor", QColor(192, 196, 204));
    m_lineEdit->setStyleSheet(
        QString("QLineEdit { background: transparent; border: none; padding: 0; color: %1; }"
                "QLineEdit::placeholder { color: %2; }")
            .arg(rgbaColor(themeColor("textColor", QColor(48, 49, 51)), QStringLiteral("#303133")),
                 rgbaColor(placeholderColor, QStringLiteral("#c0c4cc"))));
    update();
}

void HQLineEdit::setEmphasisProgress(qreal progress)
{
    progress = qBound(0.0, progress, 1.0);
    if (qFuzzyCompare(m_emphasisProgress, progress)) {
        return;
    }
    m_emphasisProgress = progress;
    update();
}

QColor HQLineEdit::themeColor(const QString &key, const QColor &fallback) const
{
    const QColor color = ThemeManager::instance().color(QStringLiteral("hqComboBox.") + key);
    return color.isValid() ? color : fallback;
}

void HQLineEdit::animateEmphasisTo(qreal value)
{
    value = qBound(0.0, value, 1.0);
    m_emphasisAnimation->stop();
    m_emphasisAnimation->setStartValue(m_emphasisProgress);
    m_emphasisAnimation->setEndValue(value);
    m_emphasisAnimation->start();
}

qreal HQLineEdit::targetEmphasis() const
{
    if (m_focused) {
        return 1.0;
    }
    return m_hovered ? 0.65 : 0.0;
}

void HQLineEdit::refreshMetrics()
{
    setFixedSize(sizeHint());
}

void HQLineEdit::updateAppearance()
{
    update();
}

int HQLineEdit::scaledPx(int designPx, int min) const
{
    const auto &ss = ScreenScale::instance();
    return DPR_INT(designPx * ss.scale(), ss.dpr(), min);
}
