#include "HelpSetting.h"
#include "widgets/HQLineEdit.h"
#include "widgets/HQToolButton.h"
#include "comm/CommonMacros.h"
#include "comm/ScreenScale.h"
#include "comm/ThemeManager.h"
#include "comm/FontManager.h"

#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QIcon>
#include <QPixmap>

class HelpDashedLine : public QWidget
{
public:
    explicit HelpDashedLine(QWidget *parent = nullptr) : QWidget(parent)
    {
        setFixedHeight(1);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QPen pen(QColor(163, 162, 164, 80), 1, Qt::DashLine);
        p.setPen(pen);
        p.drawLine(0, 0, width(), 0);
    }
};

HelpSetting::HelpSetting(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, true);
    initUI();
}

void HelpSetting::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

    const qreal w = width();
    const qreal h = height();

    const qreal radius  = DPR_REAL(16.0 * scale, dpr);
    const qreal borderW = DPR_REAL(1.0 * scale, dpr);
    const qreal halfBW  = borderW * 0.5;

    QPainterPath borderPath;
    borderPath.addRoundedRect(
        QRectF(halfBW, halfBW, w - borderW, h - borderW),
        radius - halfBW, radius - halfBW);

    painter.setPen(QPen(ThemeManager::instance().color("CollMonitor.borderColor"), borderW));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(borderPath);
}

QWidget* HelpSetting::createSeparator()
{
    return new HelpDashedLine(this);
}

QLabel* HelpSetting::createLabel(const QString &text, int fontSize, int fixedWidth)
{
    auto *label = new QLabel(text, this);
    label->setFont(FontManager::instance().font(fontSize, QFont::Normal));
    label->setStyleSheet(QStringLiteral("color: white;"));
    if (fixedWidth > 0)
        label->setFixedWidth(fixedWidth);
    return label;
}

QWidget* HelpSetting::createAuthStatusWidget()
{
    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

    auto *widget = new QWidget(this);
    auto *layout = new QHBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(DPR_INT(4 * scale, dpr, 2));

    // correct.png 图标
    QPixmap pix(QStringLiteral(":/systemSetting/correct.png"));
    if (!pix.isNull()) {
        pix.setDevicePixelRatio(dpr);
        int iconSize = DPR_INT(16 * scale, dpr, 0);
        auto *iconLbl = new QLabel(widget);
        iconLbl->setPixmap(pix);
        iconLbl->setFixedSize(iconSize, iconSize);
        iconLbl->setScaledContents(true);
        layout->addWidget(iconLbl);
    }

    // "已授权" 绿色文字
    auto *authLabel = new QLabel(QStringLiteral("已授权"), widget);
    authLabel->setFont(FontManager::instance().font(DPR_INT(13 * scale, dpr, 9), QFont::Normal));
    authLabel->setStyleSheet(QStringLiteral("color: rgb(44, 162, 91); background: transparent;"));
    layout->addWidget(authLabel);

    return widget;
}

void HelpSetting::initUI()
{
    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

    const int padInt    = DPR_INT(20 * scale, dpr, 10);
    const int spacing   = DPR_INT(16 * scale, dpr, 4);
    const int titleFs   = DPR_INT(14 * scale, dpr, 11);
    const int labelFs   = DPR_INT(13 * scale, dpr, 9);
    const int rowSpacing = DPR_INT(12 * scale, dpr, 2);

    m_labelWidth = DPR_INT(90 * scale, dpr, 60);
    const int editW = DPR_INT(300 * scale, dpr, 120);
    const int editH = DPR_INT(32 * scale, dpr, 22);

    const QColor titleColor(0xDE, 0xEF, 0xFF);
    const QString titleSs = QString("color: %1; padding: 0; margin: 0;").arg(titleColor.name());

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // ============================================================
    // 第一部分：版本信息
    // ============================================================
    {
        auto *section = new QVBoxLayout;
        section->setContentsMargins(padInt, spacing, padInt, spacing);
        section->setSpacing(rowSpacing);

        auto *title = new QLabel(QStringLiteral("版本信息"), this);
        title->setFont(FontManager::instance().font(titleFs, QFont::Bold));
        title->setStyleSheet(titleSs);
        title->setFixedHeight(DPR_INT(20 * scale, dpr, 16));
        section->addWidget(title);

        // 软件版本 + 构建日期
        {
            auto *row = new QHBoxLayout;
            row->setContentsMargins(0, 0, 0, 0);
            row->setSpacing(DPR_INT(8 * scale, dpr, 4));

            row->addWidget(createLabel(QStringLiteral("软件版本"), labelFs, m_labelWidth));

            m_softVerEdit = new HQLineEdit(this);
            m_softVerEdit->setFixedSize(editW, editH);
            m_softVerEdit->setPlaceholderText("V0.0.0");
            row->addWidget(m_softVerEdit);

            auto *buildLabel = new QLabel(QStringLiteral("(构建日期2026-07-04)"), this);
            buildLabel->setFont(FontManager::instance().font(labelFs, QFont::Normal));
            buildLabel->setStyleSheet(QStringLiteral("color: #999999;"));
            row->addWidget(buildLabel);

            row->addStretch();
            section->addLayout(row);
        }

        mainLayout->addLayout(section);
    }

    mainLayout->addWidget(createSeparator());

    // ============================================================
    // 第二部分：硬件版本
    // ============================================================
    {
        auto *section = new QVBoxLayout;
        section->setContentsMargins(padInt, spacing, padInt, spacing);
        section->setSpacing(rowSpacing);

        auto *title = new QLabel(QStringLiteral("硬件版本"), this);
        title->setFont(FontManager::instance().font(titleFs, QFont::Bold));
        title->setStyleSheet(titleSs);
        title->setFixedHeight(DPR_INT(20 * scale, dpr, 16));
        section->addWidget(title);

        // 设备型号
        {
            auto *row = new QHBoxLayout;
            row->setContentsMargins(0, 0, 0, 0);
            row->setSpacing(DPR_INT(8 * scale, dpr, 4));

            row->addWidget(createLabel(QStringLiteral("设备型号"), labelFs, m_labelWidth));

            m_deviceModelEdit = new HQLineEdit(this);
            m_deviceModelEdit->setFixedSize(editW, editH);
            m_deviceModelEdit->setPlaceholderText("BB60C");
            row->addWidget(m_deviceModelEdit);

            row->addStretch();
            section->addLayout(row);
        }

        // 硬件版本
        {
            auto *row = new QHBoxLayout;
            row->setContentsMargins(0, 0, 0, 0);
            row->setSpacing(DPR_INT(8 * scale, dpr, 4));

            row->addWidget(createLabel(QStringLiteral("硬件版本"), labelFs, m_labelWidth));

            m_hardVerEdit = new HQLineEdit(this);
            m_hardVerEdit->setFixedSize(editW, editH);
            m_hardVerEdit->setPlaceholderText("Rev 2.0");
            row->addWidget(m_hardVerEdit);

            row->addStretch();
            section->addLayout(row);
        }

        // 序列号
        {
            auto *row = new QHBoxLayout;
            row->setContentsMargins(0, 0, 0, 0);
            row->setSpacing(DPR_INT(8 * scale, dpr, 4));

            row->addWidget(createLabel(QStringLiteral("序列号"), labelFs, m_labelWidth));

            m_serialEdit = new HQLineEdit(this);
            m_serialEdit->setFixedSize(editW, editH);
            m_serialEdit->setPlaceholderText("BB6C-12345678");
            row->addWidget(m_serialEdit);

            row->addStretch();
            section->addLayout(row);
        }

        mainLayout->addLayout(section);
    }

    mainLayout->addWidget(createSeparator());

    // ============================================================
    // 第三部分：授权信息
    // ============================================================
    {
        auto *section = new QVBoxLayout;
        section->setContentsMargins(padInt, spacing, padInt, spacing);
        section->setSpacing(rowSpacing);

        auto *title = new QLabel(QStringLiteral("授权信息"), this);
        title->setFont(FontManager::instance().font(titleFs, QFont::Bold));
        title->setStyleSheet(titleSs);
        title->setFixedHeight(DPR_INT(20 * scale, dpr, 16));
        section->addWidget(title);

        // 设备型号 + 已授权状态
        {
            auto *row = new QHBoxLayout;
            row->setContentsMargins(0, 0, 0, 0);
            row->setSpacing(DPR_INT(8 * scale, dpr, 4));

            row->addWidget(createLabel(QStringLiteral("设备型号"), labelFs, m_labelWidth));

            row->addWidget(createAuthStatusWidget());

            row->addStretch();
            section->addLayout(row);
        }

        // 授权有效期
        {
            auto *row = new QHBoxLayout;
            row->setContentsMargins(0, 0, 0, 0);
            row->setSpacing(DPR_INT(8 * scale, dpr, 4));

            row->addWidget(createLabel(QStringLiteral("授权有效期"), labelFs, m_labelWidth));

            m_authDateEdit = new HQLineEdit(this);
            m_authDateEdit->setFixedSize(editW, editH);
            m_authDateEdit->setPlaceholderText(QStringLiteral("2025-01-01 至 2026-12-31"));
            m_authDateEdit->setEnabled(false);
            row->addWidget(m_authDateEdit);

            row->addStretch();
            section->addLayout(row);
        }

        // 授权功能
        {
            auto *row = new QHBoxLayout;
            row->setContentsMargins(0, 0, 0, 0);
            row->setSpacing(DPR_INT(8 * scale, dpr, 4));

            row->addWidget(createLabel(QStringLiteral("授权功能"), labelFs, m_labelWidth));

            m_authFuncEdit = new HQLineEdit(this);
            m_authFuncEdit->setFixedSize(editW, editH);
            m_authFuncEdit->setPlaceholderText(QStringLiteral("基础频谱分析/信号检测/录制回放/告警推送"));
            m_authFuncEdit->setEnabled(false);
            row->addWidget(m_authFuncEdit);

            row->addStretch();
            section->addLayout(row);
        }

        mainLayout->addLayout(section);
    }

    mainLayout->addWidget(createSeparator());

    // ============================================================
    // 第四部分：按钮
    // ============================================================
    {
        auto *section = new QVBoxLayout;
        section->setContentsMargins(padInt, spacing, padInt, spacing);
        section->setSpacing(spacing);

        auto *btnRow = new QHBoxLayout;
        btnRow->setContentsMargins(0, 0, 0, 0);
        btnRow->setSpacing(DPR_INT(16 * scale, dpr, 8));
        btnRow->setAlignment(Qt::AlignCenter);

        // 检查更新
        m_updateBtn = new HQToolButton(this);
        m_updateBtn->setText(QStringLiteral("检查更新"));
        m_updateBtn->setHQIcon(QIcon(QStringLiteral(":/systemSetting/update.png")), 14, 14);
        m_updateBtn->setFixedSize(DPR_INT(110 * scale, dpr, 70), DPR_INT(30 * scale, dpr, 22));
        m_updateBtn->setHQRadius(6);
        m_updateBtn->setTextColor(QColor(255, 255, 255));
        btnRow->addWidget(m_updateBtn);

        // 授权管理
        m_licenseBtn = new HQToolButton(this);
        m_licenseBtn->setText(QStringLiteral("授权管理"));
        m_licenseBtn->setHQIcon(QIcon(QStringLiteral(":/systemSetting/download.png")), 14, 14);
        m_licenseBtn->setFixedSize(DPR_INT(110 * scale, dpr, 70), DPR_INT(30 * scale, dpr, 22));
        m_licenseBtn->setHQRadius(6);
        m_licenseBtn->setTextColor(QColor(255, 255, 255));
        btnRow->addWidget(m_licenseBtn);

        // 灰色提示文字
        auto *hintLabel = new QLabel(QStringLiteral("(导入授权文件)"), this);
        hintLabel->setFont(FontManager::instance().font(labelFs, QFont::Normal));
        hintLabel->setStyleSheet(QStringLiteral("color: #999999;"));
        btnRow->addWidget(hintLabel);

        btnRow->addStretch();
        section->addLayout(btnRow);

        mainLayout->addLayout(section);
    }
    mainLayout->addStretch();
}
