#include "LogSetting.h"
#include "widgets/HQLineEdit.h"
#include "widgets/HQFileLineEdit.h"
#include "widgets/HQCheckBox.h"
#include "widgets/HQCComboBox.h"
#include "widgets/HQToolButton.h"
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
#include <QIcon>

class LogDashedLine : public QWidget
{
public:
    explicit LogDashedLine(QWidget *parent = nullptr) : QWidget(parent)
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

LogSetting::LogSetting(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, true);
    initUI();
}

void LogSetting::paintEvent(QPaintEvent *)
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

QWidget* LogSetting::createSeparator()
{
    return new LogDashedLine(this);
}

QLabel* LogSetting::createLabel(const QString &text, int fontSize, int fixedWidth)
{
    auto *label = new QLabel(text, this);
    label->setFont(FontManager::instance().font(fontSize, QFont::Normal));
    label->setStyleSheet(QStringLiteral("color: white;"));
    if (fixedWidth > 0)
        label->setFixedWidth(fixedWidth);
    return label;
}

void LogSetting::initUI()
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
    // 第一部分：日志导出
    // ============================================================
    {
        auto *section = new QVBoxLayout;
        section->setContentsMargins(padInt, spacing, padInt, spacing);
        section->setSpacing(rowSpacing);

        auto *title = new QLabel(QStringLiteral("日志导出"), this);
        title->setFont(FontManager::instance().font(titleFs, QFont::Bold));
        title->setStyleSheet(titleSs);
        title->setFixedHeight(DPR_INT(20 * scale, dpr, 16));
        section->addWidget(title);

        // 导出范围 — 三个单选框
        {
            auto *row = new QHBoxLayout;
            row->setContentsMargins(0, 0, 0, 0);
            row->setSpacing(DPR_INT(8 * scale, dpr, 4));

            row->addWidget(createLabel(QStringLiteral("导出范围"), labelFs, m_labelWidth));

            m_exportRangeGroup = new QButtonGroup(this);
            m_exportRangeGroup->setExclusive(true);

            auto *radioWidget = new QWidget(this);
            radioWidget->setFixedHeight(DPR_INT(19 * scale, dpr, 16));
            auto *radioLayout = new QHBoxLayout(radioWidget);
            radioLayout->setContentsMargins(0, 0, 0, 0);
            radioLayout->setSpacing(DPR_INT(20 * scale, dpr, 8));

            const QStringList ranges = {
                QStringLiteral("最近24小时"),
                QStringLiteral("最近7天"),
                QStringLiteral("全部")
            };
            for (int i = 0; i < ranges.size(); ++i) {
                auto *radio = new QRadioButton(ranges.at(i), this);
                radio->setFont(FontManager::instance().font(labelFs, QFont::Normal));
                radio->setStyleSheet(QStringLiteral(
                    "QRadioButton { color: white; padding: 0; margin: 0; spacing: 2px; }"));
                radio->setFixedHeight(DPR_INT(18 * scale, dpr, 14));
                m_exportRangeGroup->addButton(radio, i);
                radioLayout->addWidget(radio);
            }
            radioLayout->addStretch();
            row->addWidget(radioWidget);
            row->addStretch();
            section->addLayout(row);

            // 默认选中"最近24小时"
            if (m_exportRangeGroup->button(0))
                m_exportRangeGroup->button(0)->setChecked(true);
        }

        // 三个复选框（系统日志、告警日志、检测结果日志）
        {
            auto *row = new QHBoxLayout;
            row->setContentsMargins(0, 0, 0, 0);
            row->setSpacing(DPR_INT(8 * scale, dpr, 4));

            row->addWidget(createLabel(QStringLiteral("包含内容"), labelFs, m_labelWidth));

            auto *cbWidget = new QWidget(this);
            auto *cbLayout = new QHBoxLayout(cbWidget);
            cbLayout->setContentsMargins(0, 0, 0, 0);
            cbLayout->setSpacing(DPR_INT(20 * scale, dpr, 8));

            m_logSys = new HQCheckBox(cbWidget);
            m_logSys->setText(QStringLiteral("系统日志"));
            m_logSys->setTextColor(QColor(255, 255, 255));
            m_logSys->setFont(FontManager::instance().font(labelFs, QFont::Normal));
            cbLayout->addWidget(m_logSys);

            m_logAlert = new HQCheckBox(cbWidget);
            m_logAlert->setText(QStringLiteral("告警日志"));
            m_logAlert->setTextColor(QColor(255, 255, 255));
            m_logAlert->setFont(FontManager::instance().font(labelFs, QFont::Normal));
            cbLayout->addWidget(m_logAlert);

            m_logResult = new HQCheckBox(cbWidget);
            m_logResult->setText(QStringLiteral("检测结果日志"));
            m_logResult->setTextColor(QColor(255, 255, 255));
            m_logResult->setFont(FontManager::instance().font(labelFs, QFont::Normal));
            cbLayout->addWidget(m_logResult);

            cbLayout->addStretch();
            row->addWidget(cbWidget);
            row->addStretch();
            section->addLayout(row);
        }

        // 导出格式 — 下拉框
        {
            auto *row = new QHBoxLayout;
            row->setContentsMargins(0, 0, 0, 0);
            row->setSpacing(DPR_INT(8 * scale, dpr, 4));

            row->addWidget(createLabel(QStringLiteral("导出格式"), labelFs, m_labelWidth));

            m_formatCbx = new HQCComboBox(this);
            m_formatCbx->setFixedSize(editW, editH);
            m_formatCbx->addItems({QStringLiteral("CSV"), QStringLiteral("JSON"), QStringLiteral("TXT")});
            row->addWidget(m_formatCbx);

            row->addStretch();
            section->addLayout(row);
        }

        mainLayout->addLayout(section);
    }

    mainLayout->addWidget(createSeparator());

    // ============================================================
    // 第二部分：日志级别
    // ============================================================
    {
        auto *section = new QVBoxLayout;
        section->setContentsMargins(padInt, spacing, padInt, spacing);
        section->setSpacing(rowSpacing);

        auto *title = new QLabel(QStringLiteral("日志级别"), this);
        title->setFont(FontManager::instance().font(titleFs, QFont::Bold));
        title->setStyleSheet(titleSs);
        title->setFixedHeight(DPR_INT(20 * scale, dpr, 16));
        section->addWidget(title);

        // 记录级别
        {
            auto *row = new QHBoxLayout;
            row->setContentsMargins(0, 0, 0, 0);
            row->setSpacing(DPR_INT(8 * scale, dpr, 4));

            row->addWidget(createLabel(QStringLiteral("记录级别"), labelFs, m_labelWidth));

            m_levelCbx = new HQCComboBox(this);
            m_levelCbx->setFixedSize(editW, editH);
            m_levelCbx->addItems({
                QStringLiteral("调试"),
                QStringLiteral("信息"),
                QStringLiteral("警告"),
                QStringLiteral("错误")
            });
            row->addWidget(m_levelCbx);

            row->addStretch();
            section->addLayout(row);
        }

        mainLayout->addLayout(section);
    }

    mainLayout->addWidget(createSeparator());

    // ============================================================
    // 第三部分：日志文件存储位置
    // ============================================================
    {
        auto *section = new QVBoxLayout;
        section->setContentsMargins(padInt, spacing, padInt, spacing);
        section->setSpacing(rowSpacing);

        auto *title = new QLabel(QStringLiteral("日志文件存储位置"), this);
        title->setFont(FontManager::instance().font(titleFs, QFont::Bold));
        title->setStyleSheet(titleSs);
        title->setFixedHeight(DPR_INT(20 * scale, dpr, 16));
        section->addWidget(title);

        // 当前日志路径
        {
            auto *row = new QHBoxLayout;
            row->setContentsMargins(0, 0, 0, 0);
            row->setSpacing(DPR_INT(8 * scale, dpr, 4));

            row->addWidget(createLabel(QStringLiteral("当前日志路径"), labelFs, m_labelWidth));

            m_pathEdit = new HQFileLineEdit(this);
            m_pathEdit->setFixedSize(editW, editH);
            m_pathEdit->setPlaceholderText(QStringLiteral("请选择日志文件存储目录"));
            row->addWidget(m_pathEdit);

            row->addStretch();
            section->addLayout(row);
        }

        mainLayout->addLayout(section);
    }

    mainLayout->addWidget(createSeparator());

    // ============================================================
    // 第四部分：导出日志按钮
    // ============================================================
    {
        auto *section = new QHBoxLayout;
        section->setContentsMargins(padInt, spacing, padInt, spacing);
        section->setSpacing(spacing);

        m_exportBtn = new HQToolButton(this);
        m_exportBtn->setText(QStringLiteral("导出日志"));
        m_exportBtn->setHQIcon(QIcon(QStringLiteral(":/recordplayback/export.png")), 14, 14);
        m_exportBtn->setFixedSize(DPR_INT(100 * scale, dpr, 70), DPR_INT(30 * scale, dpr, 22));
        m_exportBtn->setHQRadius(6);
        m_exportBtn->setTextColor(QColor(255, 255, 255));
        section->addWidget(m_exportBtn, 0, Qt::AlignCenter);
        section->addStretch();

        mainLayout->addLayout(section);
    }
    mainLayout->addStretch();
}
