#include "PushSetting.h"
#include "widgets/HQLineEdit.h"
#include "widgets/HQCheckBox.h"
#include "widgets/HQCComboBox.h"
#include "comm/CommonMacros.h"
#include "comm/ScreenScale.h"
#include "comm/ThemeManager.h"
#include "comm/FontManager.h"

#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QButtonGroup>
#include <QRadioButton>
#include <QLabel>

class PushDashedLine : public QWidget
{
public:
    explicit PushDashedLine(QWidget *parent = nullptr) : QWidget(parent)
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

PushSetting::PushSetting(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, true);
    initUI();
}

void PushSetting::paintEvent(QPaintEvent *)
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

QWidget* PushSetting::createSeparator()
{
    return new PushDashedLine(this);
}

QLabel* PushSetting::createLabel(const QString &text, int fontSize, int fixedWidth)
{
    auto *label = new QLabel(text, this);
    label->setFont(FontManager::instance().font(fontSize, QFont::Normal));
    label->setStyleSheet(QStringLiteral("color: white;"));
    if (fixedWidth > 0)
        label->setFixedWidth(fixedWidth);
    return label;
}

void PushSetting::initUI()
{
    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

    const int padInt    = DPR_INT(20 * scale, dpr, 10);
    const int spacing   = DPR_INT(16 * scale, dpr, 4);
    const int titleFs   = DPR_INT(14 * scale, dpr, 11);
    const int labelFs   = DPR_INT(13 * scale, dpr, 9);
    const int rowSpacing = DPR_INT(12 * scale, dpr, 2);

    // 左侧标签宽度
    m_labelWidth = DPR_INT(90 * scale, dpr, 60);
    const int editW = DPR_INT(300 * scale, dpr, 120);
    const int editH = DPR_INT(32 * scale, dpr, 22);

    const QColor titleColor(0xDE, 0xEF, 0xFF);
    const QString titleSs = QString("color: %1; padding: 0; margin: 0;").arg(titleColor.name());

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // ============================================================
    // 第一部分：推送接口使能
    // ============================================================
    {
        auto *section = new QVBoxLayout;
        section->setContentsMargins(padInt, spacing, padInt, spacing);
        section->setSpacing(spacing);

        auto *title = new QLabel(QStringLiteral("推送接口使能"), this);
        title->setFont(FontManager::instance().font(titleFs, QFont::Bold));
        title->setStyleSheet(titleSs);
        title->setFixedHeight(DPR_INT(20 * scale, dpr, 16));
        section->addWidget(title);

        // 启用 UDP 推送 / 启用 REST API 推送（水平排列）
        {
            auto *cbRow = new QHBoxLayout;
            cbRow->setContentsMargins(0, 0, 0, 0);
            cbRow->setSpacing(DPR_INT(24 * scale, dpr, 12));

            m_udpCheck = new HQCheckBox(this);
            m_udpCheck->setText(QStringLiteral("启用UDP推送"));
            m_udpCheck->setTextColor(QColor(255, 255, 255));
            m_udpCheck->setFont(FontManager::instance().font(labelFs, QFont::Normal));
            cbRow->addWidget(m_udpCheck);

            m_restCheck = new HQCheckBox(this);
            m_restCheck->setText(QStringLiteral("启用REST API推送"));
            m_restCheck->setTextColor(QColor(255, 255, 255));
            m_restCheck->setFont(FontManager::instance().font(labelFs, QFont::Normal));
            cbRow->addWidget(m_restCheck);

            cbRow->addStretch();
            section->addLayout(cbRow);
        }

        mainLayout->addLayout(section);
    }

    mainLayout->addWidget(createSeparator());

    // ============================================================
    // 第二部分：UDP 配置
    // ============================================================
    {
        auto *section = new QVBoxLayout;
        section->setContentsMargins(padInt, spacing, padInt, spacing);
        section->setSpacing(rowSpacing);

        auto *title = new QLabel(QStringLiteral("UDP配置"), this);
        title->setFont(FontManager::instance().font(titleFs, QFont::Bold));
        title->setStyleSheet(titleSs);
        title->setFixedHeight(DPR_INT(20 * scale, dpr, 16));
        section->addWidget(title);

        // 目标地址
        {
            auto *row = new QHBoxLayout;
            row->setContentsMargins(0, 0, 0, 0);
            row->setSpacing(DPR_INT(8 * scale, dpr, 4));

            row->addWidget(createLabel(QStringLiteral("目标地址"), labelFs, m_labelWidth));

            m_udpAddrEdit = new HQLineEdit(this);
            m_udpAddrEdit->setFixedSize(editW, editH);
            m_udpAddrEdit->setPlaceholderText(QStringLiteral("0.0.0.0"));
            row->addWidget(m_udpAddrEdit);

            row->addStretch();
            section->addLayout(row);
        }

        // 端口号
        {
            auto *row = new QHBoxLayout;
            row->setContentsMargins(0, 0, 0, 0);
            row->setSpacing(DPR_INT(8 * scale, dpr, 4));

            row->addWidget(createLabel(QStringLiteral("端口号"), labelFs, m_labelWidth));

            m_udpPortEdit = new HQLineEdit(this);
            m_udpPortEdit->setFixedSize(editW, editH);
            m_udpPortEdit->setDoubleRange(1, 65535, 0);
            m_udpPortEdit->setPlaceholderText(QStringLiteral("8000"));
            row->addWidget(m_udpPortEdit);

            row->addStretch();
            section->addLayout(row);
        }

        // 推送内容 — 三个复选框
        {
            auto *row = new QHBoxLayout;
            row->setContentsMargins(0, 0, 0, 0);
            row->setSpacing(DPR_INT(8 * scale, dpr, 4));

            row->addWidget(createLabel(QStringLiteral("推送内容"), labelFs, m_labelWidth));

            auto *cbWidget = new QWidget(this);
            auto *cbLayout = new QHBoxLayout(cbWidget);
            cbLayout->setContentsMargins(0, 0, 0, 0);
            cbLayout->setSpacing(DPR_INT(20 * scale, dpr, 8));

            m_udpSignal = new HQCheckBox(cbWidget);
            m_udpSignal->setText(QStringLiteral("信号列表"));
            m_udpSignal->setTextColor(QColor(255, 255, 255));
            m_udpSignal->setFont(FontManager::instance().font(labelFs, QFont::Normal));
            cbLayout->addWidget(m_udpSignal);

            m_udpAlert = new HQCheckBox(cbWidget);
            m_udpAlert->setText(QStringLiteral("告警事件"));
            m_udpAlert->setTextColor(QColor(255, 255, 255));
            m_udpAlert->setFont(FontManager::instance().font(labelFs, QFont::Normal));
            cbLayout->addWidget(m_udpAlert);

            m_udpSpectrum = new HQCheckBox(cbWidget);
            m_udpSpectrum->setText(QStringLiteral("频谱数据"));
            m_udpSpectrum->setTextColor(QColor(255, 255, 255));
            m_udpSpectrum->setFont(FontManager::instance().font(labelFs, QFont::Normal));
            cbLayout->addWidget(m_udpSpectrum);

            cbLayout->addStretch();
            row->addWidget(cbWidget);
            row->addStretch();
            section->addLayout(row);
        }

        mainLayout->addLayout(section);
    }

    mainLayout->addWidget(createSeparator());

    // ============================================================
    // 第三部分：REST 配置
    // ============================================================
    {
        auto *section = new QVBoxLayout;
        section->setContentsMargins(padInt, spacing, padInt, spacing);
        section->setSpacing(rowSpacing);

        auto *title = new QLabel(QStringLiteral("REST配置"), this);
        title->setFont(FontManager::instance().font(titleFs, QFont::Bold));
        title->setStyleSheet(titleSs);
        title->setFixedHeight(DPR_INT(20 * scale, dpr, 16));
        section->addWidget(title);

        // 接口地址
        {
            auto *row = new QHBoxLayout;
            row->setContentsMargins(0, 0, 0, 0);
            row->setSpacing(DPR_INT(8 * scale, dpr, 4));

            row->addWidget(createLabel(QStringLiteral("接口地址"), labelFs, m_labelWidth));

            m_restUrlEdit = new HQLineEdit(this);
            m_restUrlEdit->setFixedSize(editW, editH);
            m_restUrlEdit->setPlaceholderText(QStringLiteral("http://"));
            row->addWidget(m_restUrlEdit);

            row->addStretch();
            section->addLayout(row);
        }

        // 接口名称
        {
            auto *row = new QHBoxLayout;
            row->setContentsMargins(0, 0, 0, 0);
            row->setSpacing(DPR_INT(8 * scale, dpr, 4));

            row->addWidget(createLabel(QStringLiteral("接口名称"), labelFs, m_labelWidth));

            m_restApiCbx = new HQCComboBox(this);
            m_restApiCbx->setFixedSize(editW, editH);
            m_restApiCbx->addItems({QStringLiteral("告警推送")});
            row->addWidget(m_restApiCbx);

            row->addStretch();
            section->addLayout(row);
        }

        // 认证方式 — 两个单选框
        {
            auto *row = new QHBoxLayout;
            row->setContentsMargins(0, 0, 0, 0);
            row->setSpacing(DPR_INT(8 * scale, dpr, 4));

            row->addWidget(createLabel(QStringLiteral("认证方式"), labelFs, m_labelWidth));

            m_authGroup = new QButtonGroup(this);
            m_authGroup->setExclusive(true);

            auto *radioWidget = new QWidget(this);
            radioWidget->setFixedHeight(DPR_INT(19 * scale, dpr, 16));
            auto *radioLayout = new QHBoxLayout(radioWidget);
            radioLayout->setContentsMargins(0, 0, 0, 0);
            radioLayout->setSpacing(DPR_INT(20 * scale, dpr, 8));

            const QStringList authModes = {
                QStringLiteral("无"),
                QStringLiteral("Bearer Token")
            };
            for (int i = 0; i < authModes.size(); ++i) {
                auto *radio = new QRadioButton(authModes.at(i), this);
                radio->setFont(FontManager::instance().font(labelFs, QFont::Normal));
                radio->setStyleSheet(QStringLiteral(
                    "QRadioButton { color: white; padding: 0; margin: 0; spacing: 2px; }"));
                radio->setFixedHeight(DPR_INT(18 * scale, dpr, 14));
                m_authGroup->addButton(radio, i);
                radioLayout->addWidget(radio);
            }
            radioLayout->addStretch();
            row->addWidget(radioWidget);
            row->addStretch();
            section->addLayout(row);

            // 默认选中"无"
            if (m_authGroup->button(0))
                m_authGroup->button(0)->setChecked(true);
        }

        mainLayout->addLayout(section);
    }
    mainLayout->addStretch();
}
