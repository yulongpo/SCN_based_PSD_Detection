#include "HQFileLineEdit.h"

#include "comm/CommonMacros.h"
#include "comm/FontManager.h"
#include "comm/ScreenScale.h"
#include "comm/ThemeManager.h"

#include <QApplication>
#include <QEvent>
#include <QFileDialog>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QPushButton>

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

HQFileLineEdit::HQFileLineEdit(QWidget *parent)
    : QWidget(parent)
    , m_lineEdit(new QLineEdit(this))
    , m_browseBtn(new QPushButton(this))
    , m_emphasisAnimation(new QPropertyAnimation(this, "emphasisProgress", this))
    , m_emphasisProgress(0.0)
    , m_hovered(false)
    , m_focused(false)
    , m_selectFile(false)
{
    setObjectName("hqFileLineEdit");
    setAttribute(Qt::WA_Hover, true);
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setFocusProxy(m_lineEdit);

    // 内部输入框：透明背景、无边框，外观完全由外层 paintEvent 绘制
    m_lineEdit->setObjectName("hqFileLineEditInner");
    m_lineEdit->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    m_lineEdit->setFrame(false);
    m_lineEdit->setContextMenuPolicy(Qt::NoContextMenu);
    m_lineEdit->setStyleSheet(
        QStringLiteral("QLineEdit { background: transparent; border: none; padding: 0; }"));
    m_lineEdit->installEventFilter(this);

    // 文件夹浏览按钮（透明、无边框，仅显示图标）
    m_browseBtn->setObjectName("hqFileBrowseBtn");
    m_browseBtn->setFlat(true);
    m_browseBtn->setCursor(Qt::PointingHandCursor);
    m_browseBtn->setIcon(QIcon(QStringLiteral(":/button/folder.png")));
    const int iconSz = scaledPx(16, 12);
    m_browseBtn->setIconSize(QSize(iconSz, iconSz));
    m_browseBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: transparent; border: none; }"));

    m_emphasisAnimation->setDuration(180);
    m_emphasisAnimation->setEasingCurve(QEasingCurve::OutCubic);

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &HQFileLineEdit::applyThemeAppearance);
    connect(m_lineEdit, &QLineEdit::textChanged,
            this, &HQFileLineEdit::textChanged);
    connect(m_lineEdit, &QLineEdit::editingFinished,
            this, &HQFileLineEdit::editingFinished);
    connect(m_browseBtn, &QPushButton::clicked,
            this, &HQFileLineEdit::onBrowse);

    refreshMetrics();
    applyThemeAppearance(ThemeManager::instance().isNightMode());
    qApp->installEventFilter(this);
}

HQFileLineEdit::~HQFileLineEdit()
{
    qApp->removeEventFilter(this);
}

QString HQFileLineEdit::text() const
{
    return m_lineEdit->text();
}

void HQFileLineEdit::setText(const QString &text)
{
    m_lineEdit->setText(text);
}

QString HQFileLineEdit::placeholderText() const
{
    return m_lineEdit->placeholderText();
}

void HQFileLineEdit::setPlaceholderText(const QString &text)
{
    m_lineEdit->setPlaceholderText(text);
}

QSize HQFileLineEdit::sizeHint() const
{
    return QSize(scaledPx(140, 80),
                 scaledPx(36, 24));
}

void HQFileLineEdit::enterEvent(QEvent *event)
{
    m_hovered = true;
    animateEmphasisTo(targetEmphasis());
    QWidget::enterEvent(event);
}

void HQFileLineEdit::leaveEvent(QEvent *event)
{
    m_hovered = false;
    animateEmphasisTo(targetEmphasis());
    QWidget::leaveEvent(event);
}

void HQFileLineEdit::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const int borderWidth = scaledPx(1, 1);
    const int radius = scaledPx(10, 4);

    // 主题色，key 前缀 "hqComboBox." 与 HQLineEdit / HQComboBox 保持一致
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

    // 使用 emphasisProgress 做平滑的颜色插值
    QColor mixedBg = QColor::fromRgbF(
        baseBg.redF()    + (bg.redF()    - baseBg.redF())    * m_emphasisProgress,
        baseBg.greenF()  + (bg.greenF()  - baseBg.greenF())  * m_emphasisProgress,
        baseBg.blueF()   + (bg.blueF()   - baseBg.blueF())   * m_emphasisProgress,
        baseBg.alphaF()  + (bg.alphaF()  - baseBg.alphaF())  * m_emphasisProgress);
    QColor mixedBorder = QColor::fromRgbF(
        borderBase.redF()   + (border.redF()   - borderBase.redF())   * m_emphasisProgress,
        borderBase.greenF() + (border.greenF() - borderBase.greenF()) * m_emphasisProgress,
        borderBase.blueF()  + (border.blueF()  - borderBase.blueF())  * m_emphasisProgress,
        borderBase.alphaF() + (border.alphaF() - borderBase.alphaF()) * m_emphasisProgress);

    const QRectF box(0.5, 0.5, width() - 1.0, height() - 1.0);
    QPainterPath path;
    path.addRoundedRect(box, radius, radius);

    if (shadowColor.isValid() && m_emphasisProgress > 0.0) {
        QColor outer = shadowColor;
        outer.setAlphaF(shadowColor.alphaF() * m_emphasisProgress * 0.8);
        painter.setPen(Qt::NoPen);
        painter.setBrush(outer);
        painter.drawRoundedRect(box.adjusted(-0.5, -0.5, 0.5, 0.5), radius + 0.5, radius + 0.5);
    }

    painter.fillPath(path, mixedBg);

    QPen borderPen(mixedBorder, borderWidth);
    borderPen.setCosmetic(true);
    painter.setPen(borderPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);
}

void HQFileLineEdit::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);

    const int leftPadding = scaledPx(12, 6);
    const int btnSz = height() - scaledPx(8, 4);
    const int btnRight = scaledPx(8, 4);

    // 浏览按钮：右侧垂直居中
    const int btnX = width() - btnRight - btnSz;
    const int btnY = (height() - btnSz) / 2;
    m_browseBtn->setGeometry(btnX, btnY, btnSz, btnSz);

    // 输入框：填充剩余区域
    const int btnMargin = scaledPx(2, 1);
    m_lineEdit->setGeometry(leftPadding, 0,
                            qMax(10, btnX - leftPadding - btnMargin),
                            height());
}

bool HQFileLineEdit::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_lineEdit) {
        if (event->type() == QEvent::FocusIn) {
            m_focused = true;
            animateEmphasisTo(targetEmphasis());
        } else if (event->type() == QEvent::FocusOut) {
            m_focused = false;
            animateEmphasisTo(targetEmphasis());
        }
    }

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

void HQFileLineEdit::changeEvent(QEvent *event)
{
    // 禁用/启用状态切换时同步更新光标
    // 由于禁用后 Qt 不向控件分发鼠标事件，仅靠 setCursor 不够，
    // 还需配合 eventFilter 中 QApplication::setOverrideCursor 全局覆盖
    if (event->type() == QEvent::EnabledChange) {
        if (isEnabled()) {
            setCursor(Qt::ArrowCursor);
            m_lineEdit->setCursor(Qt::IBeamCursor);
            m_browseBtn->setCursor(Qt::PointingHandCursor);
            // 恢复时清除全局光标覆盖
            if (m_cursorOverridden) {
                QApplication::restoreOverrideCursor();
                m_cursorOverridden = false;
            }
        } else {
            setCursor(Qt::ForbiddenCursor);
            m_lineEdit->setCursor(Qt::ForbiddenCursor);
            m_browseBtn->setCursor(Qt::ForbiddenCursor);
        }
    }
    QWidget::changeEvent(event);
}

void HQFileLineEdit::applyThemeAppearance(bool night)
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

void HQFileLineEdit::setEmphasisProgress(qreal progress)
{
    progress = qBound(0.0, progress, 1.0);
    if (qFuzzyCompare(m_emphasisProgress, progress)) {
        return;
    }
    m_emphasisProgress = progress;
    update();
}

QColor HQFileLineEdit::themeColor(const QString &key, const QColor &fallback) const
{
    const QColor color = ThemeManager::instance().color(QStringLiteral("hqComboBox.") + key);
    return color.isValid() ? color : fallback;
}

void HQFileLineEdit::animateEmphasisTo(qreal value)
{
    value = qBound(0.0, value, 1.0);
    m_emphasisAnimation->stop();
    m_emphasisAnimation->setStartValue(m_emphasisProgress);
    m_emphasisAnimation->setEndValue(value);
    m_emphasisAnimation->start();
}

qreal HQFileLineEdit::targetEmphasis() const
{
    if (m_focused) {
        return 1.0;
    }
    return m_hovered ? 0.65 : 0.0;
}

void HQFileLineEdit::refreshMetrics()
{
    setFixedSize(sizeHint());
}

int HQFileLineEdit::scaledPx(int designPx, int min) const
{
    const auto &ss = ScreenScale::instance();
    return DPR_INT(designPx * ss.scale(), ss.dpr(), min);
}

void HQFileLineEdit::setSelectFile(bool selectFile)
{
    m_selectFile = selectFile;
}

void HQFileLineEdit::onBrowse()
{
    const QString currentPath = m_lineEdit->text();

    if (m_selectFile) {
        // 文件选择模式
        const QString filePath = QFileDialog::getOpenFileName(
            this,
            QStringLiteral("选择文件"),
            currentPath,
            QStringLiteral("数据文件 (*.dat *.bin *.csv);;所有文件 (*)"));

        if (!filePath.isEmpty()) {
            m_lineEdit->setText(filePath);
            // 浏览选择完成即视为编辑完成，通知外部处理
            emit editingFinished();
        }
    } else {
        // 目录选择模式（默认）
        const QString dir = QFileDialog::getExistingDirectory(
            this,
            QStringLiteral("选择目录"),
            currentPath);

        if (!dir.isEmpty()) {
            m_lineEdit->setText(dir);
            // 浏览选择完成即视为编辑完成，通知外部处理
            emit editingFinished();
        }
    }
}
