#include "HQGeneralDialog.h"

#include "comm/CommonMacros.h"
#include "comm/FontManager.h"
#include "comm/ScreenScale.h"
#include "comm/ThemeManager.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScreen>
#include <QShowEvent>
#include <QVBoxLayout>
#include <QWindow>

// ============================================================================
// 构造 / 析构
// ============================================================================

HQGeneralDialog::HQGeneralDialog(const QString &title, QWidget *parent)
    : QDialog(parent)
{
    setObjectName("HQGeneralDialog");
    setWindowFlags(Qt::FramelessWindowHint | Qt::Dialog);
    setAttribute(Qt::WA_TranslucentBackground);
    setModal(true);
    setFocusPolicy(Qt::StrongFocus);

    int mainMargin = scaledPx(12, 12);
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(mainMargin, mainMargin, mainMargin, mainMargin);
    m_mainLayout->setSpacing(0);

    setupTitleBar(title);

    // 内容区占位布局：固定位于标题栏与底部按钮之间，子类内容添加到其中
    m_contentLayout = new QVBoxLayout;
    m_contentLayout->setContentsMargins(0, 0, 0, 0);
    m_contentLayout->setSpacing(0);
    m_mainLayout->addLayout(m_contentLayout, 1);

    setupButtons();

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &HQGeneralDialog::applyTheme);
    applyTheme();

    qApp->installEventFilter(this);
}

HQGeneralDialog::~HQGeneralDialog() = default;

// ============================================================================
// 绘制 — 圆角背景与边框
// ============================================================================

void HQGeneralDialog::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // 背景填充
    QPainterPath bgPath;
    bgPath.addRoundedRect(QRectF(rect()), kRadius, kRadius);
    p.fillPath(bgPath, QColor(14, 14, 29));

    // 边框
    const QColor bd = ThemeManager::instance().color("dialog.borderColor");
    if (bd.isValid()) {
        QPainterPath borderPath;
        borderPath.addRoundedRect(
            QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5),
            kRadius - 0.5, kRadius - 0.5);
        QPen pen(bd, scaledPx(1, 1));
        pen.setCosmetic(true);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawPath(borderPath);
    }
}

// ============================================================================
// 标题栏
// ============================================================================

void HQGeneralDialog::setupTitleBar(const QString &title)
{
    const int titleH = scaledPx(44, 30);
    const int btnSz  = scaledPx(21, 14);

    m_titleBar = new QWidget(this);
    m_titleBar->setObjectName("HQGeneralDialogTitleBar");
    m_titleBar->setFixedHeight(titleH);

    auto *lay = new QHBoxLayout(m_titleBar);
    lay->setContentsMargins(0, 0, scaledPx(8, 4), 0);
    lay->setSpacing(0);

    // 蓝色竖线
    m_accentLine = new QLabel(m_titleBar);
    m_accentLine->setObjectName("HQGeneralDialogAccentLine");
    m_accentLine->setFixedSize(scaledPx(4, 2), scaledPx(18, 12));
    m_accentLine->setStyleSheet(
        "QLabel { background-color: rgba(10,140,254,255); border-radius: 5px; border: none; }");
    lay->addWidget(m_accentLine);
    lay->addSpacing(scaledPx(10, 5));

    // 标题
    m_titleLabel = new QLabel(title, m_titleBar);
    m_titleLabel->setObjectName("HQGeneralDialogTitle");
    QFont tf = FontManager::instance().font(scaledPx(15, 10), QFont::Bold);
    m_titleLabel->setFont(tf);
    lay->addWidget(m_titleLabel);
    lay->addStretch();

    // 关闭按钮
    m_closeBtn = new QPushButton(m_titleBar);
    m_closeBtn->setObjectName("HQGeneralDialogCloseBtn");
    m_closeBtn->setFixedSize(btnSz, btnSz);
    m_closeBtn->setFlat(true);
    m_closeBtn->setCursor(Qt::PointingHandCursor);

    QPixmap cp(":/recordplayback/close.png");
    if (!cp.isNull()) {
        cp.setDevicePixelRatio(ScreenScale::instance().dpr());
        m_closeBtn->setIcon(QIcon(cp));
        m_closeBtn->setIconSize(QSize(btnSz, btnSz));
    } else {
        m_closeBtn->setText("X");
    }
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    lay->addWidget(m_closeBtn);

    m_mainLayout->addWidget(m_titleBar);
}

// ============================================================================
// 底部按钮：确定 + 取消
// ============================================================================

void HQGeneralDialog::setupButtons()
{
    const int btnW = scaledPx(116, 70);
    const int btnH = scaledPx(28, 20);
    const int btnRadius = scaledPx(12, 0);
    const int btnFs = scaledPx(13, 9);

    auto *btnLayout = new QHBoxLayout;
    btnLayout->setContentsMargins(0, scaledPx(12, 6), 0, scaledPx(16, 8));
    btnLayout->setSpacing(scaledPx(20, 12));
    btnLayout->setAlignment(Qt::AlignCenter);

    // ---- 取消按钮（渐变样式） ----
    m_cancelBtn = new QPushButton(QStringLiteral("取消"), this);
    m_cancelBtn->setCursor(Qt::PointingHandCursor);
    m_cancelBtn->setFixedSize(btnW, btnH);
    m_cancelBtn->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0,"
        "    stop:0 rgba(10, 140, 254, 90),"
        "    stop:1 rgba(10, 140, 254, 0));"
        "  border: 1px solid rgb(101, 129, 157);"
        "  border-radius: %1px;"
        "  color: white;"
        "  font-size: %2px;"
        "  padding: 0;"
        "}"
        "QPushButton:hover {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0,"
        "    stop:0 rgba(10, 140, 254, 120),"
        "    stop:1 rgba(10, 140, 254, 30));"
        "  border: 1px solid rgb(151, 179, 207);"
        "  color: #CCE8FF;"
        "}"
        "QPushButton:pressed {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0,"
        "    stop:0 rgba(10, 140, 254, 60),"
        "    stop:1 rgba(10, 140, 254, 0));"
        "  border: 1px solid rgb(81, 109, 137);"
        "  color: #A0D0FF;"
        "  padding-top: 1px;"
        "  padding-left: 1px;"
        "}").arg(btnRadius).arg(btnFs));

    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    // ---- 确定按钮（纯色背景） ----
    m_okBtn = new QPushButton(QStringLiteral("确定"), this);
    m_okBtn->setCursor(Qt::PointingHandCursor);
    m_okBtn->setFixedSize(btnW, btnH);
    m_okBtn->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background: #0A8CFE;"
        "  border: 1px solid rgb(101, 129, 157);"
        "  border-radius: %1px;"
        "  color: white;"
        "  font-size: %2px;"
        "  padding: 0;"
        "}"
        "QPushButton:hover {"
        "  background: #2A9CFF;"
        "  border: 1px solid rgb(151, 179, 207);"
        "  color: #E8F0FF;"
        "}"
        "QPushButton:pressed {"
        "  background: #0870D0;"
        "  border: 1px solid rgb(81, 109, 137);"
        "  color: #C0E0FF;"
        "  padding-top: 1px;"
        "  padding-left: 1px;"
        "}").arg(btnRadius).arg(btnFs));

    // 点击确定：调用虚函数，子类可覆盖定制行为
    connect(m_okBtn, &QPushButton::clicked, this, &HQGeneralDialog::onOkClicked);

    btnLayout->addWidget(m_cancelBtn);
    btnLayout->addWidget(m_okBtn);

    m_mainLayout->addLayout(btnLayout);
}

// ============================================================================
// 主题
// ============================================================================

void HQGeneralDialog::applyTheme()
{
    const auto &tm = ThemeManager::instance();

    if (m_titleBar)
        m_titleBar->setStyleSheet("#HQGeneralDialogTitleBar { background-color: rgb(14,14,29); }");

    if (m_titleLabel)
        m_titleLabel->setStyleSheet(QString("color: %1; background: transparent; border: none;")
            .arg(tm.color("dialog.titleColor").isValid()
                 ? tm.color("dialog.titleColor").name() : QStringLiteral("#E8E9EB")));

    if (m_accentLine)
        m_accentLine->setStyleSheet(QString("QLabel { background-color: %1; border-radius: 5px; border: none; }")
            .arg(tm.color("dialog.accentLineColor").isValid()
                 ? tm.color("dialog.accentLineColor").name() : QStringLiteral("rgba(10,140,254,255)")));

    if (m_closeBtn)
        m_closeBtn->setStyleSheet(
            QString("QPushButton { background: transparent; border: none; }"
                    "QPushButton:hover { background-color: %1; }"
                    "QPushButton:pressed { background-color: %2; }")
                .arg(tm.color("titleBar.buttonHoverTint").isValid()
                     ? tm.colorString("titleBar.buttonHoverTint") : QStringLiteral("rgba(255,255,255,24)"))
                .arg(tm.color("titleBar.buttonPressedBgColor").isValid()
                     ? tm.colorString("titleBar.buttonPressedBgColor") : QStringLiteral("rgba(255,255,255,24)")));

    update();
}

// ============================================================================
// 事件过滤 — 标题栏拖拽
// ============================================================================

bool HQGeneralDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (!isVisible())
        return QDialog::eventFilter(watched, event);

    // 标题栏拖拽
    if (m_closeBtn && watched == m_closeBtn)
        return QDialog::eventFilter(watched, event);

    if (event->type() == QEvent::MouseButtonPress) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() != Qt::LeftButton)
            return QDialog::eventFilter(watched, event);

        QWidget *w = qobject_cast<QWidget *>(watched);
        if (!w || w->window() != this)
            return QDialog::eventFilter(watched, event);

        const QRect dragZone(0, 0, width(), m_titleBar->geometry().bottom());
        if (dragZone.contains(mapFromGlobal(me->globalPos()))) {
            m_dragging = true;
            m_dragPosition = me->globalPos() - frameGeometry().topLeft();
            return true;
        }

    } else if (event->type() == QEvent::MouseMove) {
        if (!m_dragging)
            return QDialog::eventFilter(watched, event);
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->buttons() & Qt::LeftButton) {
            move(me->globalPos() - m_dragPosition);
            return true;
        }
    } else if (event->type() == QEvent::MouseButtonRelease) {
        if (m_dragging)
            m_dragging = false;
    }

    return QDialog::eventFilter(watched, event);
}

// ============================================================================
// 显示事件 — 首次居中
// ============================================================================

void HQGeneralDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);

    // 子类在构造函数中才设置尺寸，故在首次显示时按最终尺寸居中
    if (!m_centered) {
        m_centered = true;
        if (QScreen *sc = QApplication::primaryScreen())
            move(sc->availableGeometry().center() - rect().center());
    }
}

// ============================================================================
// 公共接口
// ============================================================================

void HQGeneralDialog::setDialogTitle(const QString &title)
{
    if (m_titleLabel)
        m_titleLabel->setText(title);
}

void HQGeneralDialog::setDialogSize(int width, int height)
{
    setFixedSize(width, height);
}

void HQGeneralDialog::setOkButtonText(const QString &text)
{
    if (m_okBtn)
        m_okBtn->setText(text);
}

void HQGeneralDialog::setButtonSize(int width, int height)
{
    if (m_okBtn)
        m_okBtn->setFixedSize(width, height);
    if (m_cancelBtn)
        m_cancelBtn->setFixedSize(width, height);
}

QVBoxLayout *HQGeneralDialog::contentLayout() const
{
    return m_contentLayout;
}

QPushButton *HQGeneralDialog::okButton() const
{
    return m_okBtn;
}

QPushButton *HQGeneralDialog::cancelButton() const
{
    return m_cancelBtn;
}

// ============================================================================
// 确定按钮点击 — 默认关闭并返回 Accepted
// ============================================================================

void HQGeneralDialog::onOkClicked()
{
    accept();
}

// ============================================================================
// 缩放辅助
// ============================================================================

int HQGeneralDialog::scaledPx(int designPx, int min) const
{
    const auto &ss = ScreenScale::instance();
    return DPR_INT(designPx * ss.scale(), ss.dpr(), min);
}
