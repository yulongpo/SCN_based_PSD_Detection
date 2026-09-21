#include "HQCComboBox.h"

#include "comm/CommonMacros.h"
#include "comm/FontManager.h"
#include "comm/ScreenScale.h"
#include "comm/ThemeManager.h"

#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QScreen>
#include <QVariantAnimation>
#include <QWindow>

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

// --------------- 下拉弹窗（无详情按钮版）---------------

class HQCComboPopup : public QWidget
{
public:
    explicit HQCComboPopup()
    {
        setObjectName("hqCComboPopup");
        setWindowFlags(Qt::Tool
                       | Qt::FramelessWindowHint
                       | Qt::NoDropShadowWindowHint
                       | Qt::WindowDoesNotAcceptFocus);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_ShowWithoutActivating, true);
        setFocusPolicy(Qt::NoFocus);
        setMouseTracking(true);
    }

    // 相比 HQComboPopup：去掉了 detailColor、detailHoverColor、detailButtonWidth、detailButtonHeight
    void setup(const QList<HQCComboBox::ItemEntry> &items,
               int currentIndex,
               const QFont &font,
               const QColor &textColor,
               const QColor &selectedTextColor,
               const QColor &hoverColor,
               const QColor &selectedBgColor,
               const QColor &popupBgColor,
               const QColor &popupBorderColor,
               const QColor &shadowColor,
               int itemHeight,
               int horizontalPadding,
               int popupRadius)
    {
        m_items = items;
        m_currentIndex = currentIndex;
        m_font = font;
        m_textColor = textColor;
        m_selectedTextColor = selectedTextColor;
        m_hoverColor = hoverColor;
        m_selectedBgColor = selectedBgColor;
        m_popupBgColor = popupBgColor;
        m_popupBorderColor = popupBorderColor;
        m_shadowColor = shadowColor;
        m_itemHeight = itemHeight;
        m_horizontalPadding = horizontalPadding;
        m_popupRadius = popupRadius;
        m_contentHeight = m_items.size() * m_itemHeight + m_verticalPadding * 2;
        update();
    }

    qreal popupOpacity() const { return m_popupOpacity; }
    void setPopupOpacity(qreal value) { m_popupOpacity = value; update(); }

    qreal scaleY() const { return m_scaleY; }
    void setScaleY(qreal value) { m_scaleY = value; update(); }

    int contentHeight() const { return m_contentHeight; }

    std::function<void(int)> onItemClicked;

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (m_items.isEmpty()) {
            return;
        }

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        painter.setOpacity(m_popupOpacity);

        const qreal sy = qMax(0.0, m_scaleY);
        const QRectF body(m_outerHorizontalMargin,
                          m_outerTopMargin,
                          width() - m_outerHorizontalMargin * 2.0,
                          m_contentHeight);

        painter.save();
        painter.scale(1.0, sy);

        if (m_shadowColor.alpha() > 0) {
            QColor outer = m_shadowColor;
            outer.setAlphaF(m_shadowColor.alphaF() * 0.75);
            painter.setPen(Qt::NoPen);
            painter.setBrush(outer);
            painter.drawRoundedRect(body.adjusted(-2.0, -1.0, 2.0, m_outerBottomMargin - 2.0),
                                    m_popupRadius + 2.0,
                                    m_popupRadius + 2.0);
        }

        painter.setPen(Qt::NoPen);
        painter.setBrush(m_popupBgColor);
        painter.drawRoundedRect(body, m_popupRadius, m_popupRadius);

        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(m_popupBorderColor, 1));
        painter.drawRoundedRect(body.adjusted(0.5, 0.5, -0.5, -0.5),
                                m_popupRadius - 0.5,
                                m_popupRadius - 0.5);

        painter.setFont(m_font);

        for (int i = 0; i < m_items.size(); ++i) {
            const QRectF rowRect = itemRect(i);
            if (i == m_currentIndex) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(m_selectedBgColor);
                painter.drawRoundedRect(rowRect.adjusted(4.0, 2.0, -4.0, -2.0), 6.0, 6.0);
            } else if (i == m_hoveredIndex) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(m_hoverColor);
                painter.drawRoundedRect(rowRect.adjusted(4.0, 2.0, -4.0, -2.0), 6.0, 6.0);
            }

            painter.setPen(i == m_currentIndex ? m_selectedTextColor : m_textColor);
            // 无详情按钮，文本占满行宽
            painter.drawText(rowRect.adjusted(m_horizontalPadding, 0,
                                              -m_horizontalPadding, 0),
                             Qt::AlignLeft | Qt::AlignVCenter,
                             m_items.at(i).text);
        }

        painter.restore();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        const int rowIndex = rowIndexAt(event->pos());
        if (m_hoveredIndex != rowIndex) {
            m_hoveredIndex = rowIndex;
            update();
        }
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton) {
            return;
        }
        m_pressedRowIndex = rowIndexAt(event->pos());
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton) {
            return;
        }

        const int rowIndex = rowIndexAt(event->pos());
        if (m_pressedRowIndex >= 0 && rowIndex == m_pressedRowIndex) {
            if (onItemClicked) {
                onItemClicked(m_pressedRowIndex);
            }
            m_pressedRowIndex = -1;
            event->accept();
            return;
        }

        m_pressedRowIndex = -1;
        event->accept();
    }

    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        event->accept();
    }

    void leaveEvent(QEvent *) override
    {
        if (m_hoveredIndex >= 0) {
            m_hoveredIndex = -1;
            update();
        }
        m_pressedRowIndex = -1;
    }

private:
    QRectF itemRect(int index) const
    {
        return QRectF(m_outerHorizontalMargin,
                      m_outerTopMargin + m_verticalPadding + index * m_itemHeight,
                      width() - m_outerHorizontalMargin * 2.0,
                      m_itemHeight);
    }

    int rowIndexAt(const QPoint &point) const
    {
        if (m_items.isEmpty() || m_itemHeight <= 0) {
            return -1;
        }
        const qreal sy = qMax(0.01, m_scaleY);
        const QPointF logical(point.x(), point.y() / sy);
        for (int i = 0; i < m_items.size(); ++i) {
            if (itemRect(i).contains(logical)) {
                return i;
            }
        }
        return -1;
    }

private:
    QList<HQCComboBox::ItemEntry> m_items;
    QFont m_font;
    QColor m_textColor;
    QColor m_selectedTextColor;
    QColor m_hoverColor;
    QColor m_selectedBgColor;
    QColor m_popupBgColor;
    QColor m_popupBorderColor;
    QColor m_shadowColor;
    int m_currentIndex = -1;
    int m_itemHeight = 0;
    int m_horizontalPadding = 0;
    int m_popupRadius = 0;
    int m_contentHeight = 0;
    int m_verticalPadding = 6;
    qreal m_outerHorizontalMargin = 2.0;
    qreal m_outerTopMargin = 2.0;
    qreal m_outerBottomMargin = 8.0;
    qreal m_popupOpacity = 1.0;
    qreal m_scaleY = 1.0;
    int m_hoveredIndex = -1;
    int m_pressedRowIndex = -1;
};

} // namespace

// ============================================================
//                      HQCComboBox
// ============================================================

HQCComboBox::HQCComboBox(QWidget *parent)
    : QWidget(parent)
    , m_textLabel(new QLabel(this))
    , m_popup(nullptr)
    , m_arrowAnimation(new QPropertyAnimation(this, "arrowRotation", this))
    , m_emphasisAnimation(new QPropertyAnimation(this, "emphasisProgress", this))
    , m_popupOpenAnimation(nullptr)
    , m_popupCloseAnimation(nullptr)
    , m_placeholderText(QStringLiteral("请选择"))
    , m_currentIndex(-1)
    , m_arrowRotation(0.0)
    , m_emphasisProgress(0.0)
    , m_hovered(false)
    , m_popupVisible(false)
    , m_popupAnimating(false)
    , m_fontSize(14)
{
    setObjectName("hqCComboBox");
    setAttribute(Qt::WA_Hover, true);
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    m_textLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_textLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);

    m_arrowAnimation->setDuration(220);
    m_arrowAnimation->setEasingCurve(QEasingCurve::OutCubic);

    m_emphasisAnimation->setDuration(180);
    m_emphasisAnimation->setEasingCurve(QEasingCurve::OutCubic);

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &HQCComboBox::applyThemeAppearance);

    refreshMetrics();
    updateCurrentDisplay();
    applyThemeAppearance(ThemeManager::instance().isNightMode());
    qApp->installEventFilter(this);
}

HQCComboBox::~HQCComboBox()
{
    if (m_cursorOverridden) {
        QApplication::restoreOverrideCursor();
        m_cursorOverridden = false;
    }
    qApp->removeEventFilter(this);
    if (m_popup) {
        m_popup->hide();
        m_popup->deleteLater();
        m_popup = nullptr;
    }
}

void HQCComboBox::addItem(const QString &text, const QVariant &userData)
{
    ItemEntry entry;
    entry.text = text;
    entry.data = userData;
    m_items.append(entry);

    if (m_currentIndex < 0) {
        setCurrentIndex(0);
    } else {
        updateCurrentDisplay();
    }
}

void HQCComboBox::addItems(const QStringList &texts)
{
    for (const QString &text : texts) {
        addItem(text);
    }
}

void HQCComboBox::clear()
{
    m_items.clear();
    m_currentIndex = -1;
    updateCurrentDisplay();
    hidePopup();
}

int HQCComboBox::count() const
{
    return m_items.size();
}

QString HQCComboBox::currentText() const
{
    if (m_currentIndex < 0 || m_currentIndex >= m_items.size()) {
        return QString();
    }
    return m_items.at(m_currentIndex).text;
}

QVariant HQCComboBox::currentData() const
{
    if (m_currentIndex < 0 || m_currentIndex >= m_items.size()) {
        return QVariant();
    }
    return m_items.at(m_currentIndex).data;
}

void HQCComboBox::setPlaceholderText(const QString &text)
{
    if (m_placeholderText == text) {
        return;
    }
    m_placeholderText = text;
    updateCurrentDisplay();
}

void HQCComboBox::setCurrentIndex(int index)
{
    if (index < -1 || index >= m_items.size()) {
        return;
    }

    if (m_currentIndex == index) {
        updateCurrentDisplay();
        return;
    }

    m_currentIndex = index;
    updateCurrentDisplay();
    emit currentIndexChanged(index);
    emit currentTextChanged(currentText());
}

QSize HQCComboBox::sizeHint() const
{
    return QSize(scaledPx(164, 100),
                 scaledPx(36, 24));
}

void HQCComboBox::setCusFont(int cusFont)
{
    m_fontSize = cusFont;
    updateFonts();
    update();
}

// ------ 事件处理 ------

bool HQCComboBox::eventFilter(QObject *obj, QEvent *event)
{
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

    if (!m_popup || !m_popup->isVisible()) {
        return QWidget::eventFilter(obj, event);
    }

    if (!m_cursorOverridden && event->type() == QEvent::Wheel) {
        hidePopup();
        return true;
    }

    if (!m_cursorOverridden && event->type() == QEvent::MouseButtonPress
        || event->type() == QEvent::NonClientAreaMouseButtonPress) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        const QPoint popupLocal = m_popup->mapFromGlobal(mouseEvent->globalPos());
        const QPoint selfLocal = mapFromGlobal(mouseEvent->globalPos());

        if (!m_popup->rect().contains(popupLocal) && !rect().contains(selfLocal)) {
            hidePopup();
            return true;
        }
    }

    Q_UNUSED(obj);
    return QWidget::eventFilter(obj, event);
}

void HQCComboBox::enterEvent(QEvent *event)
{
    m_hovered = true;
    animateEmphasisTo(targetEmphasis());
    QWidget::enterEvent(event);
}

void HQCComboBox::leaveEvent(QEvent *event)
{
    m_hovered = false;
    animateEmphasisTo(targetEmphasis());
    QWidget::leaveEvent(event);
}

void HQCComboBox::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (m_popupVisible) {
            hidePopup();
        } else {
            showPopup();
        }
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void HQCComboBox::applyThemeAppearance(bool night)
{
    Q_UNUSED(night);
    updateFonts();
    updateCurrentDisplay();
    update();
}

// ------ 绘制 ------

void HQCComboBox::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const int borderWidth = scaledPx(1, 1);
    const int radius = scaledPx(10, 4);

    const QColor baseBg = themeColor("backgroundColor", QColor(255, 255, 255));
    const QColor hoverBg = themeColor("hoverBackgroundColor", baseBg);
    const QColor activeBg = themeColor("activeBackgroundColor", hoverBg);
    const QColor borderBase = themeColor("borderColor", QColor(220, 223, 230));
    const QColor borderHover = themeColor("hoverBorderColor", QColor(192, 196, 204));
    const QColor borderActive = themeColor("activeBorderColor", QColor(64, 158, 255));
    const QColor shadowColor = themeColor("activeShadowColor", QColor(64, 158, 255, 38));

    QColor bg = baseBg;
    QColor border = borderBase;
    if (m_popupVisible) {
        bg = activeBg;
        border = borderActive;
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

    // 下拉箭头（与 HQComboBox 完全一致）
    const QColor arrowColor = themeColor("arrowColor", QColor(96, 98, 102));
    const QColor arrowActiveColor = themeColor("arrowActiveColor", QColor(64, 158, 255));
    QColor mixedArrow = QColor::fromRgbF(
        arrowColor.redF() + (arrowActiveColor.redF() - arrowColor.redF()) * m_emphasisProgress,
        arrowColor.greenF() + (arrowActiveColor.greenF() - arrowColor.greenF()) * m_emphasisProgress,
        arrowColor.blueF() + (arrowActiveColor.blueF() - arrowColor.blueF()) * m_emphasisProgress,
        arrowColor.alphaF() + (arrowActiveColor.alphaF() - arrowColor.alphaF()) * m_emphasisProgress);

    const int rightGap = scaledPx(18, 10);
    const int arrowX = width() - rightGap;
    const int arrowY = height() / 2;
    const int arrowSize = scaledPx(10, 6);

    painter.save();
    painter.translate(arrowX, arrowY);
    painter.rotate(m_arrowRotation);
    QPainterPath arrowPath;
    arrowPath.moveTo(-arrowSize / 2.0, -arrowSize / 4.0);
    arrowPath.lineTo(0, arrowSize / 4.0);
    arrowPath.lineTo(arrowSize / 2.0, -arrowSize / 4.0);
    painter.setPen(QPen(mixedArrow,
                        qMax(1, scaledPx(2, 1)),
                        Qt::SolidLine,
                        Qt::RoundCap,
                        Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(arrowPath);
    painter.restore();
}

void HQCComboBox::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);

    const int leftPadding = scaledPx(12, 6);
    const int arrowReserve = scaledPx(32, 18);
    m_textLabel->setGeometry(leftPadding, 0,
                             qMax(10, width() - arrowReserve - leftPadding),
                             height());
}

void HQCComboBox::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::EnabledChange) {
        if (isEnabled()) {
            setCursor(Qt::ArrowCursor);
            // 恢复时清除全局光标覆盖
            if (m_cursorOverridden) {
                QApplication::restoreOverrideCursor();
                m_cursorOverridden = false;
            }
        } else {
            setCursor(Qt::ForbiddenCursor);
        }
    }
    QWidget::changeEvent(event);
}

// ------ 动画 ------

void HQCComboBox::setArrowRotation(qreal rotation)
{
    if (qFuzzyCompare(m_arrowRotation, rotation)) {
        return;
    }
    m_arrowRotation = rotation;
    update();
}

void HQCComboBox::setEmphasisProgress(qreal progress)
{
    progress = qBound(0.0, progress, 1.0);
    if (qFuzzyCompare(m_emphasisProgress, progress)) {
        return;
    }
    m_emphasisProgress = progress;
    update();
}

QColor HQCComboBox::themeColor(const QString &key, const QColor &fallback) const
{
    const QColor color = ThemeManager::instance().color(QStringLiteral("hqComboBox.") + key);
    return color.isValid() ? color : fallback;
}

void HQCComboBox::animateArrowTo(qreal value)
{
    m_arrowAnimation->stop();
    m_arrowAnimation->setStartValue(m_arrowRotation);
    m_arrowAnimation->setEndValue(value);
    m_arrowAnimation->start();
}

void HQCComboBox::animateEmphasisTo(qreal value)
{
    value = qBound(0.0, value, 1.0);
    m_emphasisAnimation->stop();
    m_emphasisAnimation->setStartValue(m_emphasisProgress);
    m_emphasisAnimation->setEndValue(value);
    m_emphasisAnimation->start();
}

// ------ 弹出菜单 ------

void HQCComboBox::ensurePopup()
{
    if (m_popup) {
        return;
    }

    HQCComboPopup *popup = new HQCComboPopup();
    popup->hide();

    popup->onItemClicked = [this](int index) {
        if (index < 0 || index >= m_items.size()) {
            return;
        }
        setCurrentIndex(index);
        hidePopup();
    };

    m_popup = popup;

    m_popupOpenAnimation = new QVariantAnimation(this);
    m_popupOpenAnimation->setDuration(220);
    m_popupOpenAnimation->setStartValue(0.0);
    m_popupOpenAnimation->setEndValue(1.0);
    m_popupOpenAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_popupOpenAnimation, &QVariantAnimation::valueChanged, this,
            [popup](const QVariant &value) {
                popup->setScaleY(value.toReal());
                popup->setPopupOpacity(value.toReal());
            });
    connect(m_popupOpenAnimation, &QVariantAnimation::finished, this, [this]() {
        m_popupAnimating = false;
    });

    m_popupCloseAnimation = new QVariantAnimation(this);
    m_popupCloseAnimation->setDuration(160);
    m_popupCloseAnimation->setEasingCurve(QEasingCurve::InCubic);
    connect(m_popupCloseAnimation, &QVariantAnimation::valueChanged, this,
            [popup](const QVariant &value) {
                popup->setScaleY(value.toReal());
                popup->setPopupOpacity(value.toReal());
            });
    connect(m_popupCloseAnimation, &QVariantAnimation::finished, this, [this]() {
        if (m_popup) {
            m_popup->hide();
        }
        m_popupAnimating = false;
    });
}

void HQCComboBox::showPopup()
{
    if (m_popupVisible || m_popupAnimating || m_items.isEmpty()) {
        return;
    }

    ensurePopup();
    HQCComboPopup *popup = static_cast<HQCComboPopup *>(m_popup);
    if (!popup) {
        return;
    }

    const auto &tm = ThemeManager::instance();
    popup->setup(m_items,
                 m_currentIndex,
                 m_textLabel->font(),
                 themeColor("textColor", QColor(48, 49, 51)),
                 themeColor("itemSelectedTextColor", QColor(64, 158, 255)),
                 themeColor("itemHoverBackgroundColor", QColor(245, 247, 250)),
                 themeColor("itemSelectedBackgroundColor", QColor(236, 245, 255)),
                 themeColor("popupBackgroundColor", QColor(255, 255, 255)),
                 themeColor("popupBorderColor", QColor(228, 231, 237)),
                 themeColor("popupShadowColor", QColor(0, 0, 0, 28)),
                 height(),
                 scaledPx(14, 6),
                 scaledPx(12, 6));

    updatePopupGeometry();

    popup->setScaleY(0.0);
    popup->setPopupOpacity(0.0);
    popup->show();
    popup->raise();

    // 模态对话框内下拉弹窗兼容：若本控件处于模态对话框中，将弹窗的
    // transient parent 设为该对话框，绕过模态拦截，保证下拉列表项
    // 可被点击选择（HQCComboPopup 为 Qt::Tool 顶层窗口，默认会被模态拦截）
    if (window()->isModal())
    {
        if (auto *pw = popup->windowHandle())
        {
            if (auto *dw = window()->windowHandle())
                pw->setTransientParent(dw);
        }
    }

    m_popupVisible = true;
    m_popupAnimating = true;
    animateArrowTo(180.0);
    animateEmphasisTo(1.0);

    if (m_popupCloseAnimation) {
        m_popupCloseAnimation->stop();
    }
    if (m_popupOpenAnimation) {
        m_popupOpenAnimation->stop();
        m_popupOpenAnimation->start();
    }
}

void HQCComboBox::hidePopup()
{
    if (!m_popup || !m_popupVisible) {
        return;
    }

    m_popupVisible = false;
    m_popupAnimating = true;
    animateArrowTo(0.0);
    animateEmphasisTo(targetEmphasis());

    if (!m_popupOpenAnimation || !m_popupCloseAnimation) {
        m_popup->hide();
        m_popupAnimating = false;
        return;
    }

    HQCComboPopup *popup = static_cast<HQCComboPopup *>(m_popup);
    m_popupOpenAnimation->stop();
    m_popupCloseAnimation->stop();
    m_popupCloseAnimation->setStartValue(popup->scaleY());
    m_popupCloseAnimation->setEndValue(0.0);
    m_popupCloseAnimation->start();
}

void HQCComboBox::updatePopupGeometry()
{
    if (!m_popup) {
        return;
    }

    HQCComboPopup *popup = static_cast<HQCComboPopup *>(m_popup);
    if (!popup) {
        return;
    }

    const int popupWidth = width() + scaledPx(4, 2);
    const int popupHeight = qMin(popup->contentHeight() + scaledPx(10, 6),
                                 qMax(height() + scaledPx(10, 6),
                                      6 * height() + scaledPx(10, 6)));
    QPoint pos = mapToGlobal(QPoint(-scaledPx(2, 1), height() + scaledPx(1, 1)));

    QScreen *screen = QApplication::screenAt(pos);
    if (!screen) {
        screen = QApplication::primaryScreen();
    }
    if (screen) {
        const QRect screenRect = screen->availableGeometry();
        if (pos.y() + popupHeight > screenRect.bottom()) {
            pos.setY(pos.y() - height() - popupHeight - scaledPx(2, 1));
        }
        if (pos.x() + popupWidth > screenRect.right()) {
            pos.setX(screenRect.right() - popupWidth);
        }
        if (pos.x() < screenRect.left()) {
            pos.setX(screenRect.left());
        }
    }

    m_popup->setGeometry(QRect(pos, QSize(popupWidth, popupHeight)));
}

void HQCComboBox::updateCurrentDisplay()
{
    const bool hasCurrent = m_currentIndex >= 0 && m_currentIndex < m_items.size();
    const QString text = hasCurrent ? m_items.at(m_currentIndex).text : m_placeholderText;
    m_textLabel->setText(text);

    const QColor textColor = hasCurrent
        ? themeColor("textColor", QColor(48, 49, 51))
        : themeColor("placeholderColor", QColor(192, 196, 204));
    m_textLabel->setStyleSheet(QString("color: %1; background: transparent;")
        .arg(rgbaColor(textColor, QStringLiteral("#c0c4cc"))));
}

void HQCComboBox::updateFonts()
{
    m_textLabel->setFont(FontManager::instance().font(scaledPx(m_fontSize, 6), QFont::Normal));
}

void HQCComboBox::refreshMetrics()
{
    setFixedSize(sizeHint());
}

qreal HQCComboBox::targetEmphasis() const
{
    if (m_popupVisible) {
        return 1.0;
    }
    return m_hovered ? 0.65 : 0.0;
}

int HQCComboBox::scaledPx(int designPx, int min) const
{
    const auto &ss = ScreenScale::instance();
    return DPR_INT(designPx * ss.scale(), ss.dpr(), min);
}
