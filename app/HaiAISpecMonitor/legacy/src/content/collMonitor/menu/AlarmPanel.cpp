#include "AlarmPanel.h"

#include "comm/CommonMacros.h"
#include "comm/FontManager.h"
#include "comm/ScreenScale.h"
#include "comm/ThemeManager.h"

#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>

AlarmPanel::AlarmPanel(const QString &imagePath,
                       const QString &labelText,
                       const QString &numberColorPath,
                       QWidget *parent)
    : QWidget(parent)
    , m_numberColorPath(numberColorPath)
{
    const auto &ss    = ScreenScale::instance();
    const qreal dpr   = ss.dpr();
    const double scale = ss.scale();

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(DPR_REAL(20.0 * scale, dpr), 0, DPR_REAL(20.0 * scale, dpr), 0);
    layout->setSpacing(static_cast<int>(DPR_REAL(12.0 * scale, dpr)));

    // --- 左侧图标（圆角彩色背景 + 32×32 图片居中） ---
    const int iconBgSize = static_cast<int>(DPR_REAL(48.0 * scale, dpr));
    const int iconSize   = static_cast<int>(DPR_REAL(28.0 * scale, dpr));
    const int iconBgRadius = static_cast<int>(DPR_REAL(8.0 * scale, dpr));

    m_iconBg = new QWidget(this);
    m_iconBg->setFixedSize(iconBgSize, iconBgSize);
    m_iconBg->setContentsMargins(0, 0, 0, 0);
    m_iconBg->setStyleSheet(QString(
        "background-color: %1; border-radius: %2px;")
        .arg(ThemeManager::instance().colorString(m_numberColorPath))
        .arg(iconBgRadius));

    auto *iconBgLayout = new QVBoxLayout(m_iconBg);
    iconBgLayout->setContentsMargins(0, 0, 0, 0);
    iconBgLayout->setAlignment(Qt::AlignCenter);

    m_iconLabel = new QLabel(m_iconBg);
    m_iconLabel->setFixedSize(iconSize, iconSize);
    {
        QPixmap pixmap(imagePath);
        if (!pixmap.isNull()) {
            pixmap.setDevicePixelRatio(dpr);
            m_iconLabel->setPixmap(pixmap);
        }
    }
    m_iconLabel->setScaledContents(true);
    m_iconLabel->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
    iconBgLayout->addWidget(m_iconLabel);
    layout->addWidget(m_iconBg);

    // --- 右侧文字区域（上下两行） ---
    auto *textWidget = new QWidget(this);
    textWidget->setFixedHeight(iconBgSize);
    auto *textLayout = new QVBoxLayout(textWidget);
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(0);

    // 上方：灰色标签
    m_topLabel = new QLabel(labelText, textWidget);
    m_topLabel->setContentsMargins(0, 0, 0, 0);
    {
        QFont topFont = FontManager::instance().font(
            static_cast<int>(DPR_REAL(12.0 * scale, dpr)), QFont::Normal);
        m_topLabel->setFont(topFont);
    }
    m_topLabel->setFixedHeight(DPR_REAL(18.0 * scale, dpr));
    textLayout->addWidget(m_topLabel);

    // 下方：彩色数字标签
    m_numLabel = new QLabel(QStringLiteral("0"), textWidget);
    m_numLabel->setContentsMargins(0, 0, 0, 0);
    {
        QFont numFont = FontManager::instance().font(
            static_cast<int>(DPR_REAL(34.0 * scale, dpr)), QFont::Bold);
        m_numLabel->setFont(numFont);
    }
    m_numLabel->setFixedHeight(DPR_REAL(30.0 * scale, dpr));
    m_numLabel->setFixedWidth(DPR_REAL(100.0 * scale, dpr));
    textLayout->addWidget(m_numLabel);

    layout->addWidget(textWidget);

    // 初始应用主题颜色 + 响应主题切换
    applyThemeColors();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, [this]() { applyThemeColors(); });
}

QLabel *AlarmPanel::numberLabel() const
{
    return m_numLabel;
}

void AlarmPanel::applyThemeColors()
{
    const QString labelColor = ThemeManager::instance().colorString("titleBar.collectMonitorColor");
    const QString numColor   = ThemeManager::instance().colorString(m_numberColorPath);

    m_topLabel->setStyleSheet(QString(
        "color: %1; background: transparent; border: none;").arg(labelColor));
    m_numLabel->setStyleSheet(QString(
        "color: %1; background: transparent; border: none; font-weight: bold;").arg(numColor));

    // 更新图标背景色（颜色与数字一致）
    const int iconBgRadius = static_cast<int>(DPR_REAL(12.0 * ScreenScale::instance().scale(),
                                                       ScreenScale::instance().dpr()));
    m_iconBg->setStyleSheet(QString(
        "background-color: %1; border-radius: %2px;").arg(numColor).arg(iconBgRadius));

    update();   // 重绘边框
}

void AlarmPanel::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

    const qreal w = width();
    const qreal h = height();

    // 圆角半径 9px、边框宽度 1px
    const qreal radius  = DPR_REAL(9.0 * scale, dpr);
    const qreal borderW = DPR_REAL(1.5 * scale, dpr);
    const qreal halfBW  = borderW * 0.5;

    QPainterPath borderPath;
    borderPath.addRoundedRect(
        QRectF(halfBW, halfBW, w - borderW, h - borderW),
        radius - halfBW, radius - halfBW);

    // 背景填充 9,13,24
    QPainterPath bgPath;
    bgPath.addRoundedRect(rect(), radius, radius);
    painter.fillPath(bgPath, QColor(9, 13, 24));

    painter.setPen(QPen(ThemeManager::instance().color("CollMonitor.borderColor"), borderW));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(borderPath);
}
