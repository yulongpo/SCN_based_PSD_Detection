#include "customtitlebar.h"
#include "comm/CommonMacros.h"
#include "comm/ScreenScale.h"
#include "comm/ThemeManager.h"
#include "widgets/HQLineEdit.h"
#include "widgets/HQSpinBox.h"
#include "widgets/HQCComboBox.h"
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QPainter>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScreen>

#include "comm/FontManager.h"

QPixmap trimmedActiveBackground(const QString &path, qreal dpr)
{
    QPixmap pix(path);
    if (pix.isNull()) return QPixmap();

    QImage image = pix.toImage().convertToFormat(QImage::Format_ARGB32);
    QRect bounds;
    const auto isBackgroundPixel = [](const QColor &color) {
        return color.alpha() == 0 || (color.red() > 245 && color.green() > 245 && color.blue() > 245);
    };

    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (!isBackgroundPixel(QColor::fromRgba(image.pixel(x, y)))) {
                const QRect pixelRect(x, y, 1, 1);
                bounds = bounds.isNull() ? pixelRect : bounds.united(pixelRect);
            }
        }
    }

    if (!bounds.isValid()) {
        pix.setDevicePixelRatio(dpr);
        return pix;
    }

    QPixmap result = QPixmap::fromImage(image.copy(bounds));
    result.setDevicePixelRatio(dpr);
    return result;
}

class NavToolButton : public QToolButton
{
public:
    explicit NavToolButton(QWidget *parent = nullptr)
        : QToolButton(parent)
    {
        const auto &ss = ScreenScale::instance();
        m_activeBg = trimmedActiveBackground(":/title/btn_active.png", ss.dpr());
    }

    void setStateIcons(const QIcon &normalIcon, const QIcon &activeIcon)
    {
        m_normalIcon = normalIcon;
        m_activeIcon = activeIcon;
        updateStateIcon();
    }

protected:
    void enterEvent(QEvent *event) override
    {
        QToolButton::enterEvent(event);
        updateStateIcon();
        updateBgAnimation();
    }

    void leaveEvent(QEvent *event) override
    {
        QToolButton::leaveEvent(event);
        updateStateIcon();
        updateBgAnimation();
    }

    void nextCheckState() override
    {
        QToolButton::nextCheckState();
        updateStateIcon();
        updateBgAnimation();
    }

    void checkStateSet() override
    {
        QToolButton::checkStateSet();
        updateStateIcon();
        updateBgAnimation();
    }

    void paintEvent(QPaintEvent *event) override
    {
        // 使用动画透明度绘制背景，实现淡入淡出效果
        if (!m_activeBg.isNull() && m_bgOpacity > 0.0) {
            QPainter painter(this);
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            painter.setOpacity(m_bgOpacity);

            const QSize bgSize = m_activeBg.size() / m_activeBg.devicePixelRatio();
            if (!bgSize.isEmpty()) {
                const int targetW = width();
                const int targetH = qMin(height(), qRound(static_cast<qreal>(bgSize.height()) * targetW / bgSize.width()));
                painter.drawPixmap(QRect(0, height() - targetH, targetW, targetH), m_activeBg);
            }
        }

        QToolButton::paintEvent(event);
    }

private:
    void updateStateIcon()
    {
        const QIcon &stateIcon = (isChecked() || underMouse()) ? m_activeIcon : m_normalIcon;
        if (!stateIcon.isNull()) {
            QToolButton::setIcon(stateIcon);
        }
    }

    void updateBgAnimation()
    {
        startBgAnimation((isChecked() || underMouse()) ? 1.0 : 0.0);
    }

    void startBgAnimation(qreal targetOpacity)
    {
        // 停止旧动画并延迟删除（避免与 DeleteWhenStopped 产生 double-free）
        if (m_bgAnim) {
            m_bgAnim->stop();
            m_bgAnim->deleteLater();
            m_bgAnim = nullptr;
        }

        // 如果当前值已与目标值一致，跳过
        if (qFuzzyCompare(m_bgOpacity, targetOpacity))
            return;

        auto *anim = new QVariantAnimation(this);
        anim->setDuration(300);
        anim->setStartValue(m_bgOpacity);
        anim->setEndValue(targetOpacity);
        anim->setEasingCurve(QEasingCurve::OutCubic);

        // 动画帧更新透明度并刷新控件
        connect(anim, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
            m_bgOpacity = value.toReal();
            update();
        });

        m_bgAnim = anim;
        anim->start();
    }

    QPixmap m_activeBg;
    QIcon m_normalIcon;
    QIcon m_activeIcon;
    qreal m_bgOpacity = 0.0;
    QVariantAnimation *m_bgAnim = nullptr;
};

QPixmap tintPixmap(const QPixmap &source, const QColor &color)
{
    if (source.isNull()) {
        return QPixmap();
    }

    QPixmap tinted(source.size());
    tinted.setDevicePixelRatio(source.devicePixelRatio());
    tinted.fill(Qt::transparent);

    QPainter painter(&tinted);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.drawPixmap(0, 0, source);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(tinted.rect(), color);
    painter.end();

    return tinted;
}

HoverTinter::HoverTinter(QPushButton *button, const QColor &hoverColor,
                         int duration, QObject *parent)
    : QObject(parent)
    , m_button(button)
    , m_overlay(new QWidget(button->parentWidget()))
    , m_effect(new QGraphicsOpacityEffect(m_overlay))
    , m_anim(new QPropertyAnimation(m_effect, "opacity", this))
{
    m_overlay->setAttribute(Qt::WA_TransparentForMouseEvents);
    setHoverColor(hoverColor);
    m_overlay->stackUnder(button);
    syncGeometry();

    m_effect->setOpacity(0.0);
    m_overlay->setGraphicsEffect(m_effect);

    m_anim->setDuration(duration);
    m_anim->setEasingCurve(QEasingCurve::OutCubic);

    button->installEventFilter(this);
}

void HoverTinter::setHoverColor(const QColor &hoverColor)
{
    m_overlay->setStyleSheet(QString(
        "background-color: rgba(%1, %2, %3, %4);")
        .arg(hoverColor.red())
        .arg(hoverColor.green())
        .arg(hoverColor.blue())
        .arg(hoverColor.alpha()));
}

bool HoverTinter::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_button) {
        if (event->type() == QEvent::Enter) {
            m_anim->stop();
            m_anim->setStartValue(m_effect->opacity());
            m_anim->setEndValue(1.0);
            m_anim->start();
        } else if (event->type() == QEvent::Leave) {
            m_anim->stop();
            m_anim->setStartValue(m_effect->opacity());
            m_anim->setEndValue(0.0);
            m_anim->start();
        } else if (event->type() == QEvent::Resize
                   || event->type() == QEvent::Move) {
            syncGeometry();
        }
    }
    return QObject::eventFilter(obj, event);
}

void HoverTinter::syncGeometry()
{
    const QPoint pos = m_button->mapTo(m_button->parentWidget(),
                                       QPoint(0, 0));
    m_overlay->setGeometry(QRect(pos, m_button->size()));
}

CustomTitleBar::CustomTitleBar(QWidget *parent)
    : QWidget(parent)
    , m_iconLabel(nullptr)
    , m_minButton(nullptr)
    , m_maxButton(nullptr)
    , m_closeButton(nullptr)
    , m_layout(nullptr)
    , m_minHoverTinter(nullptr)
    , m_maxHoverTinter(nullptr)
    , m_closeHoverTinter(nullptr)
    , m_isMaximized(false)
    , m_isNightMode(true)
    , m_backgroundColor(ThemeManager::instance().color("titleBar.backgroundColor"))
{
    setObjectName("customTitleBar");
    setAttribute(Qt::WA_StyledBackground, true);
    loadIcons();
    setupUi();
    // 主题切换时刷新所有依赖主题颜色的样式
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &CustomTitleBar::applyThemeAppearance);
}

CustomTitleBar::~CustomTitleBar()
{
}

void CustomTitleBar::loadIcons()
{
    // 这里直接保留原始像素图，后续根据深浅主题动态重新着色，
    // 可以避免为明暗两套标题栏图标分别准备资源文件。
    const qreal dpr = devicePixelRatioF();

    auto loadPixmap = [dpr](const QString &path) -> QPixmap {
        QPixmap pix(path);
        if (pix.isNull()) {
            return QPixmap();
        }
        pix.setDevicePixelRatio(dpr);
        return pix;
    };

    m_minPixmap   = loadPixmap(":/title/min.png");
    m_maxPixmap   = loadPixmap(":/title/max.png");
    m_closePixmap = loadPixmap(":/title/close.png");
}

void CustomTitleBar::setupUi()
{
    const auto &ss       = ScreenScale::instance();
    const qreal dpr      = ss.dpr();
    const double scale   = ss.scale();
    const int titleH = DPR_INT(50.0 * scale, dpr, 0);
    const int logoH  = qMax(16, static_cast<int>(titleH * 3.0 / 5.0));
    const int btnW   = qMax(20, titleH);                               // 按钮与标题栏等高（正方形）
    const int iconSz = static_cast<int>(titleH / 3);

    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(DPR_INT(12 * scale, dpr, 0), 0, 0, 0);
    m_layout->setSpacing(0);

    QPixmap logoPixmap(":/title/title_log.png");
    if (!logoPixmap.isNull()) {
        logoPixmap.setDevicePixelRatio(dpr);
    }
    m_iconLabel = new QLabel(this);
    m_iconLabel->setObjectName("titleBarIcon");
    m_iconLabel->setFixedHeight(logoH);
    m_iconLabel->setPixmap(logoPixmap.scaledToHeight(
        static_cast<int>(logoH * dpr), Qt::SmoothTransformation));
    m_iconLabel->setContentsMargins(0, 0, DPR_INT(6 * scale, dpr, 0), 0);

    m_minButton = new QPushButton(this);
    m_minButton->setObjectName("titleBarMinButton");
    m_minButton->setFixedSize(btnW, titleH);
    m_minButton->setFlat(true);
    m_minButton->setIconSize(QSize(iconSz, iconSz));
    connect(m_minButton, &QPushButton::clicked, this, &CustomTitleBar::minimizeClicked);

    m_maxButton = new QPushButton(this);
    m_maxButton->setObjectName("titleBarMaxButton");
    m_maxButton->setFixedSize(btnW, titleH);
    m_maxButton->setFlat(true);
    m_maxButton->setIconSize(QSize(iconSz, iconSz));
    connect(m_maxButton, &QPushButton::clicked, this, &CustomTitleBar::maximizeRestoreClicked);

    m_closeButton = new QPushButton(this);
    m_closeButton->setObjectName("titleBarCloseButton");
    m_closeButton->setFixedSize(btnW, titleH);
    m_closeButton->setFlat(true);
    m_closeButton->setIconSize(QSize(iconSz, iconSz));
    connect(m_closeButton, &QPushButton::clicked, this, &CustomTitleBar::closeClicked);

    // 悬停高亮层单独放在按钮下方。
    // 这样无论 QSS 如何切主题，都能得到一致且平滑的淡入淡出效果。
    const auto &tm = ThemeManager::instance();
    m_minHoverTinter   = new HoverTinter(m_minButton,
        tm.color("titleBar.buttonHoverTint"), 200, this);
    m_maxHoverTinter   = new HoverTinter(m_maxButton,
        tm.color("titleBar.buttonHoverTint"), 200, this);
    m_closeHoverTinter = new HoverTinter(m_closeButton,
        tm.color("titleBar.closeButtonHoverTint"), 200, this);

    QFont titleFont = FontManager::instance().font(DPR_INT(26 * scale, dpr, 9), QFont::Bold);
    titleFont.setWeight(QFont::Bold);
    titleFont.setStyle(QFont::StyleNormal);
    titleFont.setLetterSpacing(QFont::PercentageSpacing, 120);
    titleFont.setFamily(u8"黑体");

    m_titleLabel = new QLabel(QStringLiteral("智能频谱监测仪"), this);
    m_titleLabel->setFont(titleFont);
    m_titleLabel->setStyleSheet(
        QString("color: %1; background: transparent; border: none;")
            .arg(tm.colorString("titleBar.titleColor")));
    m_titleLabel->setContentsMargins(DPR_INT(6 * scale, dpr, 0), 0, DPR_INT(6 * scale, dpr, 0), 0);

    m_titleLabel->setContentsMargins(0, 0, DPR_INT(6 * scale, dpr, 0), 0);
    m_titleLabel->setFixedHeight(logoH);

    m_layout->addWidget(m_iconLabel);
    m_layout->addWidget(m_titleLabel);

    m_layout->addStretch();
    // ----- 顶部导航按钮组：主页、新建、打开、设置（互斥选中）-----
    m_CollectBtn  = createNavButton(QStringLiteral(":/title/collect_btn.png"),
                                    QStringLiteral(":/title/collect_btn_mouseOver.png"),
                                    QStringLiteral("采集监测"));
    m_playbackBtn = createNavButton(QStringLiteral(":/title/playback.png"),
                                    QStringLiteral(":/title/playback_mouseOver.png"),
                                    QStringLiteral("录制回放"));
    m_settingBtn  = createNavButton(QStringLiteral(":/title/setting.png"),
                                    QStringLiteral(":/title/setting_mouseOver.png"),
                                    QStringLiteral("系统设置"));
    // 信号
    connect(m_CollectBtn, &QPushButton::clicked, this, &CustomTitleBar::collectClicked);
    connect(m_playbackBtn, &QPushButton::clicked, this, &CustomTitleBar::playbackClicked);
    connect(m_settingBtn, &QPushButton::clicked, this, &CustomTitleBar::settingClicked);
    // 所有导航按钮水平填满侧边栏宽度
    m_CollectBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    m_playbackBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    m_settingBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    // 四个导航按钮互斥选中，默认选中"主页"
    m_navGroup = new QButtonGroup(this);
    m_navGroup->setExclusive(true);
    m_CollectBtn->setCheckable(true);
    m_playbackBtn->setCheckable(true);
    m_settingBtn->setCheckable(true);
    m_navGroup->addButton(m_CollectBtn);
    m_navGroup->addButton(m_playbackBtn);
    m_navGroup->addButton(m_settingBtn);
    m_CollectBtn->setChecked(true);

    m_layout->addWidget(m_CollectBtn);
    m_layout->addWidget(m_playbackBtn);
    m_layout->addWidget(m_settingBtn);


    m_layout->addWidget(m_minButton);
    m_layout->addWidget(m_maxButton);
    m_layout->addWidget(m_closeButton);

    setFixedHeight(titleH);
}

void CustomTitleBar::setWindowTitle(const QString &title)
{
    Q_UNUSED(title);
}

void CustomTitleBar::setMaximized(bool maximized)
{
    m_isMaximized = maximized;
}

void CustomTitleBar::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.fillRect(rect(), m_backgroundColor);
}

void CustomTitleBar::applyThemeAppearance(bool night)
{
    // 根据当前主题更新标题栏的外观（背景色、按钮图标颜色、悬停/点击高亮色等）
    m_isNightMode = night;
    m_backgroundColor = ThemeManager::instance().color("titleBar.backgroundColor");

    // 按钮点击高亮背景色（QPushButton:pressed 状态）
    const QString pressedBg = ThemeManager::instance()
        .colorString("titleBar.buttonPressedBgColor");
    const QString pressedStyle = QString(
        "QPushButton:pressed { background-color: %1; border: none; }").arg(pressedBg);
    m_minButton->setStyleSheet(pressedStyle);
    m_maxButton->setStyleSheet(pressedStyle);
    m_closeButton->setStyleSheet(pressedStyle);

    m_titleLabel->setStyleSheet(
        QString("color: %1; background: transparent; border: none;")
            .arg(ThemeManager::instance().colorString("titleBar.titleColor")));

    updateHoverTintColors(night);
    updateButtonIcons(night);
    update();
}

void CustomTitleBar::updateHoverTintColors(bool night)
{
    // 标题栏按钮的悬停层属于主题皮肤的一部分，主题切换时统一更新
    const auto &tm = ThemeManager::instance();

    if (m_minHoverTinter) {
        m_minHoverTinter->setHoverColor(tm.color("titleBar.buttonHoverTint"));
    }
    if (m_maxHoverTinter) {
        m_maxHoverTinter->setHoverColor(tm.color("titleBar.buttonHoverTint"));
    }
    if (m_closeHoverTinter) {
        m_closeHoverTinter->setHoverColor(tm.color("titleBar.closeButtonHoverTint"));
    }
}

void CustomTitleBar::updateButtonIcons(bool night)
{
    // 暗色标题栏使用浅色图标，浅色标题栏使用深色图标，
    // 保证标题栏控制按钮在两套主题下都有稳定对比度。
    const QColor iconColor = ThemeManager::instance().color("titleBar.iconColor");

    m_minButton->setIcon(QIcon(tintPixmap(m_minPixmap, iconColor)));
    m_maxButton->setIcon(QIcon(tintPixmap(m_maxPixmap, iconColor)));
    m_closeButton->setIcon(QIcon(tintPixmap(m_closePixmap, iconColor)));
}

QToolButton *CustomTitleBar::createNavButton(const QString &iconPath,
                                      const QString &hoverIconPath,
                                      const QString &text)
{
    auto *btn = new NavToolButton(this);
    btn->setCursor(Qt::PointingHandCursor);

    const auto &ss       = ScreenScale::instance();
    const qreal dpr      = ss.dpr();
    const double scale   = ss.scale();

    // 图标+文字按钮：图标在左，文字在右
    btn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    QFont navFont = FontManager::instance().font(DPR_INT(14 * scale, dpr, 0), QFont::Medium);
    btn->setFont(navFont);
    const int iconSz = DPR_INT(16 * scale, dpr, 0);
    btn->setIconSize(QSize(iconSz, iconSz));
    const int btnH = DPR_INT(56 * scale, dpr, 0);
    btn->setFixedHeight(btnH);

    const QString displayText = QString(" %1").arg(text);
    btn->setText(displayText);

    QFontMetrics fm(navFont);
    int textW = fm.horizontalAdvance(displayText);
    int sidebarW = DPR_INT(104 * scale, dpr, 0);
    // 内容宽度 ≈ 图标 + 间距(4px) + 文字宽
    int contentW = iconSz + DPR_INT(4 * scale, dpr, 0) + textW;
    int leftPad = qMax(0, (sidebarW - contentW) / 2);
    btn->setProperty("_navLeftPad", leftPad);
    const QString btnActiveColor = ThemeManager::instance()
        .colorString("sidebar.navButtonActiveColor");
    const QString btnTextColor = ThemeManager::instance()
        .colorString("sidebar.buttonTextColor");
    btn->setStyleSheet(QString(
        "QToolButton {"
        "  padding-left: %1px;"
        "  padding-right: %1px;"
        "  border: none;"
        "  background: transparent;"
        "  font-weight: bold;"
        "  color: %2;"
        "}"
        "QToolButton:hover, QToolButton:checked {"
        "  color: %3;"
        "  border: none;"
        "  background: transparent;"
        "}"
    ).arg(leftPad).arg(btnTextColor).arg(btnActiveColor));

    // 加载普通态和悬停态图标
    QPixmap normalPix(iconPath);
    QPixmap hoverPix(hoverIconPath);
    if (!normalPix.isNull()) normalPix.setDevicePixelRatio(dpr);
    if (!hoverPix.isNull())  hoverPix.setDevicePixelRatio(dpr);

    QIcon normalIcon;
    normalIcon.addPixmap(normalPix, QIcon::Normal, QIcon::Off);
    QIcon activeIcon;
    activeIcon.addPixmap(hoverPix, QIcon::Normal, QIcon::Off);
    btn->setStateIcons(normalIcon, activeIcon);

    return btn;
}

