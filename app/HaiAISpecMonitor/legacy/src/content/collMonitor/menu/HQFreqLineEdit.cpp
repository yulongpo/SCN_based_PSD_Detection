#include "HQFreqLineEdit.h"

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
#include <QTimer>

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

// ============================================================================
// 频率单位解析辅助
// ============================================================================

/** 频率单位信息 */
struct FreqUnitInfo {
    double   multiplier    = 1.0;   // 转换为 Hz 的乘数（Hz=1, kHz=1e3, MHz=1e6, GHz=1e9）
    int      decimalPlaces = 0;     // 显示用小数位数（Hz=0, kHz=3, MHz=6, GHz=9）
    QString  displayName{"Hz"};           // 显示名称（"Hz"/"kHz"/"MHz"/"GHz"）
};

/**
 * @brief 根据单位首字母（不区分大小写）获取单位信息
 * @param firstChar 单位字符串的首字母
 * @return 对应的 FreqUnitInfo，无法识别时默认返回 Hz
 */
FreqUnitInfo freqUnitInfoFromChar(QChar firstChar)
{
    FreqUnitInfo info;
    switch (firstChar.toUpper().unicode()) {
    case 'H':
        // Hz：已由默认构造设置完毕
        break;
    case 'K':
        info.multiplier    = 1000.0;
        info.decimalPlaces = 3;
        info.displayName   = QStringLiteral("kHz");
        break;
    case 'M':
        info.multiplier    = 1000000.0;
        info.decimalPlaces = 6;
        info.displayName   = QStringLiteral("MHz");
        break;
    case 'G':
        info.multiplier    = 1000000000.0;
        info.decimalPlaces = 9;
        info.displayName   = QStringLiteral("GHz");
        break;
    default:
        // 无法识别的单位首字母，保持默认 Hz
        break;
    }
    return info;
}

/**
 * @brief 根据 Hz 绝对值自动选择最合适的显示单位
 * @param absHz 频率绝对值（单位：Hz）
 * @return 对应的 FreqUnitInfo
 */
FreqUnitInfo freqUnitInfoFromHz(double absHz)
{
    if (absHz >= 1e9) {
        return { 1000000000.0, 9, QStringLiteral("GHz") };
    } else if (absHz >= 1e6) {
        return { 1000000.0, 6, QStringLiteral("MHz") };
    } else if (absHz >= 1e3) {
        return { 1000.0, 3, QStringLiteral("kHz") };
    } else {
        return { 1.0, 0, QStringLiteral("Hz") };
    }
}

} // namespace

HQFreqLineEdit::HQFreqLineEdit(QWidget *parent)
    : QWidget(parent)
    , m_lineEdit(new QLineEdit(this))
    , m_emphasisAnimation(new QPropertyAnimation(this, "emphasisProgress", this))
    , m_emphasisProgress(0.0)
    , m_hovered(false)
    , m_focused(false)
{
    setObjectName("HQFreqLineEdit");
    setAttribute(Qt::WA_Hover, true);
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setFocusProxy(m_lineEdit); // 将焦点代理到内部 QLineEdit，点击外部时自动失去焦点

    // 内部输入框：透明背景、无边框，外观完全由外层 paintEvent 绘制
    m_lineEdit->setObjectName("HQFreqLineEditInner");
    m_lineEdit->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    m_lineEdit->setFrame(false);
    m_lineEdit->setCursor(Qt::IBeamCursor);  // 悬停时显示文本编辑光标
    // 禁用右键弹出菜单（复制/粘贴/剪切等），由上层业务决定是否需要这些功能
    m_lineEdit->setContextMenuPolicy(Qt::NoContextMenu);
    m_lineEdit->setStyleSheet(
        QStringLiteral("QLineEdit { background: transparent; border: none; padding: 0; }"));

    // 监听内部输入框的焦点变化
    m_lineEdit->installEventFilter(this);

    m_emphasisAnimation->setDuration(180);
    m_emphasisAnimation->setEasingCurve(QEasingCurve::OutCubic);

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &HQFreqLineEdit::applyThemeAppearance);
    connect(m_lineEdit, &QLineEdit::textChanged,
            this, &HQFreqLineEdit::textChanged);
    connect(m_lineEdit, &QLineEdit::editingFinished,
            this, &HQFreqLineEdit::editingFinished);
    connect(m_lineEdit, &QLineEdit::editingFinished,
            this, &HQFreqLineEdit::formatDecimalPlaces);

    refreshMetrics();
    updateAppearance();
    applyThemeAppearance(ThemeManager::instance().isNightMode());
    qApp->installEventFilter(this);
}

HQFreqLineEdit::~HQFreqLineEdit()
{
    // 析构时如有残留的全局光标覆盖，恢复默认
    if (m_cursorOverridden) {
        QApplication::restoreOverrideCursor();
        m_cursorOverridden = false;
    }
    qApp->removeEventFilter(this);
}

QString HQFreqLineEdit::text() const
{
    return m_lineEdit->text();
}

void HQFreqLineEdit::setText(const QString &text)
{
    m_lineEdit->setText(text);
}

QString HQFreqLineEdit::placeholderText() const
{
    return m_lineEdit->placeholderText();
}

void HQFreqLineEdit::setPlaceholderText(const QString &text)
{
    m_lineEdit->setPlaceholderText(text);
}

int64_t HQFreqLineEdit::value() const
{
    // 获取输入文本并去除前后空格
    const QString rawText = m_lineEdit->text().trimmed();
    if (rawText.isEmpty()) return 0.0;

    // ---- 1. 分离数字部分与单位后缀 ----
    // 从右侧向左扫描，找到连续字母的起始位置作为单位后缀
    int unitStart = -1;
    for (int i = rawText.length() - 1; i >= 0; --i) {
        if (rawText[i].isLetter()) {
            unitStart = i;
        } else {
            break;
        }
    }

    const QString numberPart = (unitStart >= 0)
        ? rawText.left(unitStart).trimmed()
        : rawText;
    const QString unitSuffix = (unitStart >= 0)
        ? rawText.mid(unitStart)
        : QString();

    // ---- 2. 将数字部分转换为浮点数 ----
    bool ok = false;
    const double number = numberPart.toDouble(&ok);
    if (!ok) return 0.0;

    // ---- 3. 根据单位后缀获取乘数，转换为 Hz 后返回 ----
    const FreqUnitInfo unitInfo = unitSuffix.isEmpty()
        ? FreqUnitInfo()   // 默认 Hz
        : freqUnitInfoFromChar(unitSuffix.at(0));

    return number * unitInfo.multiplier;
}

void HQFreqLineEdit::setValue(int64_t hz)
{
    // ---- 1. 根据 Hz 绝对值自动选择最合适的显示单位 ----
    const FreqUnitInfo unitInfo = freqUnitInfoFromHz(qAbs(hz));

    // ---- 2. 将 Hz 值转换为目标单位的显示值 ----
    const double displayValue = hz / unitInfo.multiplier;

    // ---- 3. 生成格式化文本：数字(带精度) + 空格 + 单位 ----
    const QString formatted = QString::number(displayValue, 'f', unitInfo.decimalPlaces)
                              + QLatin1Char(' ')
                              + unitInfo.displayName;

    // 仅在文本确实发生变化时才更新，避免不必要的信号触发
    if (formatted != m_lineEdit->text()) {
        m_lineEdit->setText(formatted);
    }
}

void HQFreqLineEdit::formatDecimalPlaces()
{
    // 获取输入文本并去除前后空格
    const QString rawText = m_lineEdit->text().trimmed();
    if (rawText.isEmpty()) return;

    // ---- 1. 分离数字部分与单位后缀 ----
    // 从右侧向左扫描，找到连续字母的起始位置作为单位后缀
    int unitStart = -1;
    for (int i = rawText.length() - 1; i >= 0; --i) {
        if (rawText[i].isLetter()) {
            unitStart = i;
        } else {
            break;  // 遇到非字母字符（数字、小数点、空格等），停止扫描
        }
    }

    QString numberPart;   // 数字部分（去除单位后的纯数字字符串）
    QString unitSuffix;   // 用户输入的单位部分（可能为缩写，如 "k"、"mhz" 等）

    if (unitStart >= 0) {
        numberPart = rawText.left(unitStart).trimmed();
        unitSuffix = rawText.mid(unitStart);
    } else {
        numberPart = rawText;
        // 未输入任何单位，unitSuffix 保持为空，后续按默认 Hz 处理
    }

    // ---- 2. 将数字部分转换为浮点数 ----
    bool ok = false;
    const double value = numberPart.toDouble(&ok);
    if (!ok)
    {
        m_lineEdit->blockSignals(true);
        m_lineEdit->setText("0 Hz");
        m_lineEdit->blockSignals(false);
        return;
    }

    // ---- 3. 根据单位后缀确定小数精度和格式化单位字符串 ----
    // 匹配规则：取单位首字母（不区分大小写），H→Hz, K→kHz, M→MHz, G→GHz
    // 精度规则：Hz→0位, kHz→3位, MHz→6位, GHz→9位（与10的幂次对应：1=10^0, k=10^3, M=10^6, G=10^9）
    const FreqUnitInfo unitInfo = unitSuffix.isEmpty()
        ? FreqUnitInfo()   // 默认 Hz
        : freqUnitInfoFromChar(unitSuffix.at(0));

    // ---- 4. 生成格式化文本：数字(带精度) + 空格 + 单位 ----
    const QString formatted = QString::number(value, 'f', unitInfo.decimalPlaces)
                              + QLatin1Char(' ')
                              + unitInfo.displayName;

    // 仅在文本确实发生变化时才更新，避免不必要的信号触发
    if (formatted != m_lineEdit->text()) {
        m_lineEdit->blockSignals(true);
        m_lineEdit->setText(formatted);
        m_lineEdit->blockSignals(false);
    }
}

QSize HQFreqLineEdit::sizeHint() const
{
    return QSize(scaledPx(140, 80),
                 scaledPx(36, 24));
}

void HQFreqLineEdit::enterEvent(QEvent *event)
{
    m_hovered = true;
    animateEmphasisTo(targetEmphasis());
    QWidget::enterEvent(event);
}

void HQFreqLineEdit::leaveEvent(QEvent *event)
{
    m_hovered = false;
    animateEmphasisTo(targetEmphasis());
    QWidget::leaveEvent(event);
}

void HQFreqLineEdit::changeEvent(QEvent *event)
{
    // 禁用/启用状态切换时同步更新光标
    // 由于禁用后 Qt 不向控件分发鼠标事件，仅靠 setCursor 不够，
    // 还需配合 eventFilter 中 QApplication::setOverrideCursor 全局覆盖
    if (event->type() == QEvent::EnabledChange) {
        if (isEnabled()) {
            setCursor(Qt::ArrowCursor);
            m_lineEdit->setCursor(Qt::IBeamCursor);  // 内部输入框使用文本编辑光标
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

void HQFreqLineEdit::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const int borderWidth = scaledPx(1, 1);
    const int radius = scaledPx(10, 4);

    // 主题色，key 前缀 "HQFreqLineEdit." 与 HQComboBox 的 "hqComboBox." 对称
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
}

void HQFreqLineEdit::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);

    const int leftPadding = scaledPx(12, 6);
    m_lineEdit->setGeometry(leftPadding, 0,
                            qMax(10, width() - leftPadding),
                            height());
}

bool HQFreqLineEdit::eventFilter(QObject *obj, QEvent *event)
{
    // 处理内部 QLineEdit 的焦点变化（更新自绘外观）
    if (obj == m_lineEdit) {
        if (event->type() == QEvent::FocusIn) {
            m_focused = true;
            animateEmphasisTo(targetEmphasis());
        } else if (event->type() == QEvent::FocusOut) {
            m_focused = false;
            animateEmphasisTo(targetEmphasis());
        } else if (event->type() == QEvent::MouseButtonPress) {
            // 点击输入框时全选文本
            if (isEnabled()) QTimer::singleShot(0, m_lineEdit, &QLineEdit::selectAll);
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

void HQFreqLineEdit::applyThemeAppearance(bool night)
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

void HQFreqLineEdit::setEmphasisProgress(qreal progress)
{
    progress = qBound(0.0, progress, 1.0);
    if (qFuzzyCompare(m_emphasisProgress, progress)) {
        return;
    }
    m_emphasisProgress = progress;
    update();
}

QColor HQFreqLineEdit::themeColor(const QString &key, const QColor &fallback) const
{
    const QColor color = ThemeManager::instance().color(QStringLiteral("hqComboBox.") + key);
    return color.isValid() ? color : fallback;
}

void HQFreqLineEdit::animateEmphasisTo(qreal value)
{
    value = qBound(0.0, value, 1.0);
    m_emphasisAnimation->stop();
    m_emphasisAnimation->setStartValue(m_emphasisProgress);
    m_emphasisAnimation->setEndValue(value);
    m_emphasisAnimation->start();
}

qreal HQFreqLineEdit::targetEmphasis() const
{
    if (m_focused) {
        return 1.0;
    }
    return m_hovered ? 0.65 : 0.0;
}

void HQFreqLineEdit::refreshMetrics()
{
    setFixedSize(sizeHint());
}

void HQFreqLineEdit::updateAppearance()
{
    update();
}

int HQFreqLineEdit::scaledPx(int designPx, int min) const
{
    const auto &ss = ScreenScale::instance();
    return DPR_INT(designPx * ss.scale(), ss.dpr(), min);
}
