#include "AlertRoleDialog.h"

#include "comm/CommonMacros.h"
#include "comm/FontManager.h"
#include "comm/ScreenScale.h"
#include "comm/ThemeManager.h"
#include "widgets/HQLineEdit.h"
#include "widgets/HQCComboBox.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>
#include <QWindow>

AlertRoleDialog::AlertRoleDialog(QWidget *parent)
    : QDialog(parent)
{
    setObjectName("AlertRoleDialog");
    setWindowFlags(Qt::FramelessWindowHint | Qt::Dialog);
    setAttribute(Qt::WA_TranslucentBackground);
    setModal(true);
    setFocusPolicy(Qt::StrongFocus);

    // 固定弹窗尺寸
    setFixedSize(scaledPx(740, 740), scaledPx(500, 500));

    int mainMargin = scaledPx(12, 0);
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(mainMargin, mainMargin, mainMargin, mainMargin);
    m_mainLayout->setSpacing(0);

    setupTitleBar();
    setupForm();
    setupButtons();

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &AlertRoleDialog::applyTheme);
    applyTheme();

    // 始终在屏幕中央弹出
    if (QScreen *sc = QApplication::primaryScreen())
        move(sc->availableGeometry().center() - rect().center());

    qApp->installEventFilter(this);
}

void AlertRoleDialog::paintEvent(QPaintEvent *event)
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

void AlertRoleDialog::setupTitleBar()
{
    const int titleH = scaledPx(44, 30);
    const int btnSz  = scaledPx(21, 14);

    m_titleBar = new QWidget(this);
    m_titleBar->setObjectName("AlertRoleDialogTitleBar");
    m_titleBar->setFixedHeight(titleH);

    auto *lay = new QHBoxLayout(m_titleBar);
    lay->setContentsMargins(0, 0, scaledPx(8, 4), 0);
    lay->setSpacing(0);

    // 蓝色竖线
    m_accentLine = new QLabel(m_titleBar);
    m_accentLine->setObjectName("AlertRoleDialogAccentLine");
    m_accentLine->setFixedSize(scaledPx(4, 2), scaledPx(18, 12));
    m_accentLine->setStyleSheet(
        "QLabel { background-color: rgba(10,140,254,255); border-radius: 5px; border: none; }");
    lay->addWidget(m_accentLine);
    lay->addSpacing(scaledPx(10, 5));

    // 标题
    m_titleLabel = new QLabel(QStringLiteral("新增告警规则"), m_titleBar);
    m_titleLabel->setObjectName("AlertRoleDialogTitle");
    QFont tf = FontManager::instance().font(scaledPx(15, 10), QFont::Bold);
    m_titleLabel->setFont(tf);
    lay->addWidget(m_titleLabel);
    lay->addStretch();

    // 关闭按钮
    m_closeBtn = new QPushButton(m_titleBar);
    m_closeBtn->setObjectName("AlertRoleDialogCloseBtn");
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
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::close);
    lay->addWidget(m_closeBtn);

    m_mainLayout->addWidget(m_titleBar);
}

void AlertRoleDialog::setupForm()
{
    const int formMargin    = scaledPx(16, 8);
    const int rowSpacing    = scaledPx(6, 0);
    const int colSpacing    = scaledPx(16, 8);
    const int labelFs       = scaledPx(13, 9);
    const int labelHeight   = scaledPx(20, 9);
    const int contentHeight = scaledPx(32, 9);

    // ---- 表单容器 ----
    auto *formWidget = new QWidget(this);
    auto *formLayout = new QVBoxLayout(formWidget);
    formLayout->setContentsMargins(formMargin, scaledPx(8, 4), formMargin, 0);
    formLayout->setSpacing(rowSpacing);

    QFont labelFont = FontManager::instance().font(labelFs, QFont::Normal);
    const QString labelColor = "color: rgba(255,255,255,0.8); background: transparent;";

    // ============================================================
    // 1. 规则名称*（红色*）
    // ============================================================
    {
        auto *nameLabel = new QLabel(formWidget);
        nameLabel->setFont(labelFont);
        nameLabel->setStyleSheet(labelColor);
        nameLabel->setFixedHeight(labelHeight);
        // 规则名称后跟红色星号，通过 HTML 方式实现
        const QString starColor = ThemeManager::instance().colorString("dialog.requiredColor");
        nameLabel->setText(QStringLiteral("规则名称<span style='color: %1;'>*</span>").arg(
            starColor.isEmpty() ? QStringLiteral("#FF4444") : starColor));
        nameLabel->setTextFormat(Qt::RichText);
        formLayout->addWidget(nameLabel);

        m_nameEdit = new HQLineEdit(formWidget);
        m_nameEdit->setPlaceholderText(QStringLiteral("请输入规则名称"));
        m_nameEdit->setMinimumWidth(0);
        m_nameEdit->setMaximumWidth(QWIDGETSIZE_MAX);
        m_nameEdit->setFixedHeight(contentHeight);
        m_nameEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        formLayout->addWidget(m_nameEdit);
    }

    // ============================================================
    // 2. 起始频率 + 截止频率（同一行）
    // ============================================================
    {
        auto *freqRow = new QWidget(formWidget);
        auto *freqHLay = new QHBoxLayout(freqRow);
        freqHLay->setContentsMargins(0, 0, 0, 0);
        freqHLay->setSpacing(colSpacing);

        // 起始频率
        {
            auto *colWidget = new QWidget(freqRow);
            auto *colLayout = new QVBoxLayout(colWidget);
            colLayout->setContentsMargins(0, 0, 0, 0);
            colLayout->setSpacing(scaledPx(4, 2));

            auto *label = new QLabel(QStringLiteral("起始频率"), colWidget);
            label->setFont(labelFont);
            label->setStyleSheet(labelColor);
            label->setFixedHeight(labelHeight);
            colLayout->addWidget(label);

            m_freqStart = new HQLineEdit(colWidget);
            m_freqStart->setUnit(QStringLiteral("MHz"));
            m_freqStart->setPlaceholderText(QStringLiteral("0"));
            m_freqStart->setFixedHeight(contentHeight);
            colLayout->addWidget(m_freqStart);

            freqHLay->addWidget(colWidget, 1);
        }

        // 截止频率
        {
            auto *colWidget = new QWidget(freqRow);
            auto *colLayout = new QVBoxLayout(colWidget);
            colLayout->setContentsMargins(0, 0, 0, 0);
            colLayout->setSpacing(scaledPx(4, 2));

            auto *label = new QLabel(QStringLiteral("截止频率"), colWidget);
            label->setFont(labelFont);
            label->setStyleSheet(labelColor);
            label->setFixedHeight(labelHeight);
            colLayout->addWidget(label);

            m_freqEnd = new HQLineEdit(colWidget);
            m_freqEnd->setUnit(QStringLiteral("MHz"));
            m_freqEnd->setPlaceholderText(QStringLiteral("1000"));
            m_freqEnd->setFixedHeight(contentHeight);
            colLayout->addWidget(m_freqEnd);

            freqHLay->addWidget(colWidget, 1);
        }

        formLayout->addWidget(freqRow);
    }

    // ============================================================
    // 3. 信号类型 + 告警等级（同一行）
    // ============================================================
    {
        auto *typeRow = new QWidget(formWidget);
        auto *typeHLay = new QHBoxLayout(typeRow);
        typeHLay->setContentsMargins(0, 0, 0, 0);
        typeHLay->setSpacing(colSpacing);

        // 信号类型
        {
            auto *colWidget = new QWidget(typeRow);
            auto *colLayout = new QVBoxLayout(colWidget);
            colLayout->setContentsMargins(0, 0, 0, 0);
            colLayout->setSpacing(scaledPx(4, 2));

            auto *label = new QLabel(QStringLiteral("信号类型"), colWidget);
            label->setFont(labelFont);
            label->setStyleSheet(labelColor);
            label->setFixedHeight(labelHeight);
            colLayout->addWidget(label);

            m_sigTypeCbx = new HQCComboBox(colWidget);
            m_sigTypeCbx->addItems({QStringLiteral("常在"), QStringLiteral("突发")});
            m_sigTypeCbx->setFixedHeight(contentHeight);
            colLayout->addWidget(m_sigTypeCbx);

            typeHLay->addWidget(colWidget, 1);
        }

        // 告警等级
        {
            auto *colWidget = new QWidget(typeRow);
            auto *colLayout = new QVBoxLayout(colWidget);
            colLayout->setContentsMargins(0, 0, 0, 0);
            colLayout->setSpacing(scaledPx(4, 2));

            auto *label = new QLabel(QStringLiteral("告警等级"), colWidget);
            label->setFont(labelFont);
            label->setStyleSheet(labelColor);
            label->setFixedHeight(labelHeight);
            colLayout->addWidget(label);

            m_alertLvlCbx = new HQCComboBox(colWidget);
            m_alertLvlCbx->addItems({QStringLiteral("一般"), QStringLiteral("严重")});
            m_alertLvlCbx->setFixedHeight(contentHeight);
            colLayout->addWidget(m_alertLvlCbx);

            typeHLay->addWidget(colWidget, 1);
        }

        formLayout->addWidget(typeRow);
    }

    // ============================================================
    // 4. 信号最大带宽 + 信号最小带宽（同一行）
    // ============================================================
    {
        auto *bwRow = new QWidget(formWidget);
        auto *bwHLay = new QHBoxLayout(bwRow);
        bwHLay->setContentsMargins(0, 0, 0, 0);
        bwHLay->setSpacing(colSpacing);

        // 信号最大带宽
        {
            auto *colWidget = new QWidget(bwRow);
            auto *colLayout = new QVBoxLayout(colWidget);
            colLayout->setContentsMargins(0, 0, 0, 0);
            colLayout->setSpacing(scaledPx(4, 2));

            auto *label = new QLabel(QStringLiteral("信号最大带宽"), colWidget);
            label->setFont(labelFont);
            label->setStyleSheet(labelColor);
            label->setFixedHeight(labelHeight);
            colLayout->addWidget(label);

            m_bwMaxEdit = new HQLineEdit(colWidget);
            m_bwMaxEdit->setUnit(QStringLiteral("kHz"));
            m_bwMaxEdit->setPlaceholderText(QStringLiteral("--"));
            m_bwMaxEdit->setFixedHeight(contentHeight);
            colLayout->addWidget(m_bwMaxEdit);

            bwHLay->addWidget(colWidget, 1);
        }

        // 信号最小带宽
        {
            auto *colWidget = new QWidget(bwRow);
            auto *colLayout = new QVBoxLayout(colWidget);
            colLayout->setContentsMargins(0, 0, 0, 0);
            colLayout->setSpacing(scaledPx(4, 2));

            auto *label = new QLabel(QStringLiteral("信号最小带宽"), colWidget);
            label->setFont(labelFont);
            label->setStyleSheet(labelColor);
            label->setFixedHeight(labelHeight);
            colLayout->addWidget(label);

            m_bwMinEdit = new HQLineEdit(colWidget);
            m_bwMinEdit->setUnit(QStringLiteral("kHz"));
            m_bwMinEdit->setPlaceholderText(QStringLiteral("--"));
            m_bwMinEdit->setFixedHeight(contentHeight);
            colLayout->addWidget(m_bwMinEdit);

            bwHLay->addWidget(colWidget, 1);
        }

        formLayout->addWidget(bwRow);
    }

    // ============================================================
    // 5. 备注（单独一行）
    // ============================================================
    {
        auto *remarkLabel = new QLabel(QStringLiteral("备注"), formWidget);
        remarkLabel->setFont(labelFont);
        remarkLabel->setStyleSheet(labelColor);
        remarkLabel->setFixedHeight(labelHeight);
        formLayout->addWidget(remarkLabel);

        m_remarkEdit = new HQLineEdit(formWidget);
        m_remarkEdit->setPlaceholderText(QStringLiteral("可选"));
        m_remarkEdit->setMinimumWidth(0);
        m_remarkEdit->setMaximumWidth(QWIDGETSIZE_MAX);
        m_remarkEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_remarkEdit->setFixedHeight(contentHeight);
        formLayout->addWidget(m_remarkEdit);
    }

    m_mainLayout->addWidget(formWidget, 1);
}

// ============================================================================
// 底部按钮
// ============================================================================

void AlertRoleDialog::setupButtons()
{
    const int btnW = scaledPx(116, 70);
    const int btnH = scaledPx(36, 24);
    const int btnRadius = scaledPx(12, 0);
    const int btnFs = scaledPx(13, 9);

    auto *btnLayout = new QHBoxLayout;
    btnLayout->setContentsMargins(0, scaledPx(12, 6), 0, scaledPx(16, 8));
    btnLayout->setSpacing(scaledPx(20, 12));
    btnLayout->setAlignment(Qt::AlignCenter);

    // ---- 取消按钮（使用新增规则按钮的渐变样式） ----
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

    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::close);

    // ---- 保存规则按钮（使用纯色背景，与取消按钮保持相同圆角） ----
    m_saveBtn = new QPushButton(QStringLiteral("保存规则"), this);
    m_saveBtn->setCursor(Qt::PointingHandCursor);
    m_saveBtn->setFixedSize(btnW, btnH);
    m_saveBtn->setStyleSheet(QStringLiteral(
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

    connect(m_saveBtn, &QPushButton::clicked, this, [this]() {
        // 简单校验：规则名称不能为空
        if (m_nameEdit->text().trimmed().isEmpty()) {
            return;
        }
        accept();
    });

    btnLayout->addWidget(m_cancelBtn);
    btnLayout->addWidget(m_saveBtn);

    m_mainLayout->addLayout(btnLayout);
}

// ============================================================================
// 主题
// ============================================================================

void AlertRoleDialog::applyTheme()
{
    const auto &tm = ThemeManager::instance();

    if (m_titleBar)
        m_titleBar->setStyleSheet("#AlertRoleDialogTitleBar { background-color: rgb(14,14,29); }");

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
// 事件过滤 — 标题栏拖拽 + 下拉弹窗兼容
// ============================================================================

bool AlertRoleDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (!isVisible())
        return QDialog::eventFilter(watched, event);

    // ---- 将 HQCComboBox 弹窗的 transient parent 设为对话框，绕过模态拦截 ----
    if (event->type() == QEvent::Show && !m_fixingPopup) {
        if (auto *w = qobject_cast<QWidget *>(watched)) {
            if (w->objectName() == "hqCComboPopup" && !w->parent()) {
                m_fixingPopup = true;
                if (auto *pw = w->windowHandle()) {
                    if (auto *dw = windowHandle()) {
                        pw->setTransientParent(dw);
                    }
                }
                w->raise();
                m_fixingPopup = false;
            }
        }
    }

    // ---- 标题栏拖拽 ----
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
// 获取表单数据
// ============================================================================

QString AlertRoleDialog::ruleName() const
{
    return m_nameEdit ? m_nameEdit->text().trimmed() : QString();
}

QString AlertRoleDialog::freqStart() const
{
    return m_freqStart ? m_freqStart->text().trimmed() : QString();
}

QString AlertRoleDialog::freqEnd() const
{
    return m_freqEnd ? m_freqEnd->text().trimmed() : QString();
}

QString AlertRoleDialog::sigType() const
{
    return m_sigTypeCbx ? m_sigTypeCbx->currentText() : QString();
}

QString AlertRoleDialog::alertLevel() const
{
    return m_alertLvlCbx ? m_alertLvlCbx->currentText() : QString();
}

QString AlertRoleDialog::bwMax() const
{
    return m_bwMaxEdit ? m_bwMaxEdit->text().trimmed() : QString();
}

QString AlertRoleDialog::bwMin() const
{
    return m_bwMinEdit ? m_bwMinEdit->text().trimmed() : QString();
}

QString AlertRoleDialog::remark() const
{
    return m_remarkEdit ? m_remarkEdit->text().trimmed() : QString();
}

void AlertRoleDialog::setDialogTitle(const QString &title)
{
    if (m_titleLabel)
        m_titleLabel->setText(title);
}

void AlertRoleDialog::setEditData(const QString &name, const QString &freqStart,
                                   const QString &freqEnd, const QString &sigType,
                                   int lvlMode, const QString &bwMax,
                                   const QString &bwMin, const QString &remark)
{
    if (m_nameEdit)   m_nameEdit->setText(name);
    if (m_freqStart)  m_freqStart->setText(freqStart);
    if (m_freqEnd)    m_freqEnd->setText(freqEnd);
    if (m_bwMaxEdit)  m_bwMaxEdit->setText(bwMax);
    if (m_bwMinEdit)  m_bwMinEdit->setText(bwMin);
    if (m_remarkEdit) m_remarkEdit->setText(remark);

    // 设置信号类型下拉：遍历查找匹配项
    if (m_sigTypeCbx) {
        const int savedIdx = m_sigTypeCbx->currentIndex();
        int foundIdx = -1;
        for (int i = 0; i < m_sigTypeCbx->count(); ++i) {
            m_sigTypeCbx->setCurrentIndex(i);
            if (m_sigTypeCbx->currentText() == sigType) {
                foundIdx = i;
                break;
            }
        }
        m_sigTypeCbx->setCurrentIndex(foundIdx >= 0 ? foundIdx : savedIdx);
    }

    // 告警等级：2=一般（索引0），3=严重（索引1）
    if (m_alertLvlCbx)
        m_alertLvlCbx->setCurrentIndex((lvlMode == 3) ? 1 : 0);

    setDialogTitle(QStringLiteral("编辑告警规则"));
}

int AlertRoleDialog::alertLevelMode() const
{
    // "一般" = 2, "严重" = 3，与 AlertLevelDelegate 保持一致
    return (m_alertLvlCbx && m_alertLvlCbx->currentIndex() == 1) ? 3 : 2;
}

int AlertRoleDialog::scaledPx(int designPx, int min) const
{
    const auto &ss = ScreenScale::instance();
    return DPR_INT(designPx * ss.scale(), ss.dpr(), min);
}
