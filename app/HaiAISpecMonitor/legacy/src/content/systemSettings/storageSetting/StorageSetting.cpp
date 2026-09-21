#include "StorageSetting.h"
#include "BaseDef.h"
#include "widgets/HQLineEdit.h"
#include "widgets/HQFileLineEdit.h"
#include "widgets/ToastWidget.h"
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
#include <QDir>
#include <QMouseEvent>
#include "widgets/HQSwitch.h"

StorageSetting::StorageSetting(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, true);
    initUI();
}

void StorageSetting::paintEvent(QPaintEvent *)
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

void StorageSetting::initUI()
{
    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

    const int padInt   = DPR_INT(20 * scale, dpr, 10);
    const int spacing  = DPR_INT(8 * scale, dpr, 4);
    const int titleFs  = DPR_INT(15 * scale, dpr, 11);
    const int labelFs  = DPR_INT(13 * scale, dpr, 9);
    // 左侧标签固定宽度（取所有标签中最宽值），使右侧控件对齐
    m_labelWidth = DPR_INT(110 * scale, dpr, 80);
    const int editW    = DPR_INT(300 * scale, dpr, 120);
    const int editH    = DPR_INT(32 * scale, dpr, 22);

    const QColor titleColor(0xDE, 0xEF, 0xFF);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // ============================================================
    // 第一部分：标题 — 存储策略
    // ============================================================
    auto *section1 = new QVBoxLayout;
    section1->setContentsMargins(padInt, spacing, padInt, spacing);
    section1->setSpacing(spacing);

    m_title = new QLabel(QStringLiteral("存储策略"), this);
    m_title->setFont(FontManager::instance().font(titleFs, QFont::Bold));
    m_title->setStyleSheet(QString("color: %1; padding: 0; margin: 0;").arg(titleColor.name()));
    m_title->setFixedHeight(DPR_INT(20 * scale, dpr, 16));
    section1->addWidget(m_title);

    mainLayout->addLayout(section1);

    // ============================================================
    // 第二部分：单文件最大大小
    // ============================================================
    auto *section2 = new QVBoxLayout;
    section2->setContentsMargins(padInt, spacing, padInt, spacing);
    section2->setSpacing(spacing);

    {
        auto *row = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(DPR_INT(8 * scale, dpr, 4));

        auto *label = new QLabel(QStringLiteral("单文件最大大小"), this);
        label->setFont(FontManager::instance().font(labelFs, QFont::Normal));
        label->setStyleSheet(QStringLiteral("color: white;"));
        label->setFixedWidth(m_labelWidth);
        row->addWidget(label);

        m_maxSizeEdit = new HQLineEdit(this);
        m_maxSizeEdit->setFixedSize(editW, editH);
        m_maxSizeEdit->setUnit(QStringLiteral("MB"));
        m_maxSizeEdit->setDoubleRange(1, 99999, 0);
        m_maxSizeEdit->setText(QStringLiteral("1024"));
        row->addWidget(m_maxSizeEdit);

        row->addStretch();
        section2->addLayout(row);
    }

    mainLayout->addLayout(section2);

    // ============================================================
    // 第三部分：存储溢出策略
    // ============================================================
    auto *section3 = new QVBoxLayout;
    section3->setContentsMargins(padInt, spacing, padInt, spacing);
    section3->setSpacing(spacing);

    {
        auto *row = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(DPR_INT(8 * scale, dpr, 4));

        auto *label = new QLabel(QStringLiteral("存储溢出策略:"), this);
        label->setFont(FontManager::instance().font(labelFs, QFont::Normal));
        label->setStyleSheet(QStringLiteral("color: white;"));
        label->setFixedWidth(m_labelWidth);
        row->addWidget(label);

        m_overflowGroup = new QButtonGroup(this);
        m_overflowGroup->setExclusive(true);

        auto *radioWidget = new QWidget(this);
        radioWidget->setFixedHeight(DPR_INT(19 * scale, dpr, 16));
        auto *radioLayout = new QHBoxLayout(radioWidget);
        radioLayout->setContentsMargins(0, 0, 0, 0);
        radioLayout->setSpacing(DPR_INT(16 * scale, dpr, 8));

        const QStringList modes = {
            QStringLiteral("自动覆盖"),
            QStringLiteral("退出录制")
        };
        for (int i = 0; i < modes.size(); ++i) {
            auto *radio = new QRadioButton(modes.at(i), this);
            radio->setFont(FontManager::instance().font(labelFs, QFont::Normal));
            radio->setStyleSheet(QStringLiteral(
                "QRadioButton { color: white; padding: 0; margin: 0; spacing: 2px; }"));
            radio->setFixedHeight(DPR_INT(18 * scale, dpr, 14));
            m_overflowGroup->addButton(radio, i);
            radioLayout->addWidget(radio);
        }
        radioLayout->addStretch();
        row->addWidget(radioWidget);

        row->addStretch();
        section3->addLayout(row);

        // 默认选中"自动覆盖"
        m_overflowGroup->button(0)->setChecked(true);

        // 存储溢出告警开关（与左侧标签对齐）
        {
            auto *alarmRow = new QHBoxLayout;
            alarmRow->setContentsMargins(0, 0, 0, 0);
            alarmRow->setSpacing(DPR_INT(8 * scale, dpr, 4));

            auto *alarmLabel = new QLabel(QStringLiteral("存储溢出告警"), this);
            alarmLabel->setFont(FontManager::instance().font(labelFs, QFont::Normal));
            alarmLabel->setStyleSheet(QStringLiteral("color: white;"));
            alarmLabel->setFixedWidth(m_labelWidth);
            alarmRow->addWidget(alarmLabel);

            m_overflowAlarmSwitch = new HQSwitch(QString(), this);
            alarmRow->addWidget(m_overflowAlarmSwitch);
            alarmRow->addStretch();
            section3->addLayout(alarmRow);
        }
    }

    mainLayout->addLayout(section3);

    // ============================================================
    // 第四部分：默认存储路径配置（单个固定目录）
    // ============================================================
    auto *section4 = new QVBoxLayout;
    section4->setContentsMargins(padInt, spacing, padInt, spacing);
    section4->setSpacing(spacing);

    {
        auto *row = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(DPR_INT(8 * scale, dpr, 4));

        auto *pathTitle = new QLabel(QStringLiteral("默认存储路径配置"), this);
        pathTitle->setFont(FontManager::instance().font(labelFs, QFont::Normal));
        pathTitle->setStyleSheet(QStringLiteral("color: white;"));
        pathTitle->setFixedWidth(m_labelWidth);
        row->addWidget(pathTitle);

        // 单个固定的目录选择框（替代原多级目录动态增删）
        m_pathEdit = new HQFileLineEdit(this);
        m_pathEdit->setFixedSize(DPR_INT(600 * scale, dpr, 200), editH);
        row->addWidget(m_pathEdit);
        // 路径编辑完成时校验并下发参数
        connect(m_pathEdit, &HQFileLineEdit::editingFinished,
                this, &StorageSetting::onPathEditFinished);

        row->addStretch();
        section4->addLayout(row);
    }

    mainLayout->addLayout(section4);
    mainLayout->addStretch();
}

void StorageSetting::setSignalRepo(const std::map<std::string, std::string>& params)
{
    // 保存一份 HASM_SignalRepo 存储配置参数（当前生效状态，用于路径变化比较与回退）
    m_params = params;

    // 在后端下发的频谱数据存储路径输入框展示
    auto it = m_params.find("repo_dir");
    if (it != m_params.end() && m_pathEdit) {
        m_pathEdit->setText(QString::fromUtf8(it->second.data(), static_cast<int>(it->second.size())));
    }

    // 以下存储策略控件暂未启用，统一置为不可用
    if (m_maxSizeEdit) {
        m_maxSizeEdit->setEnabled(false);
    }
    if (m_overflowGroup) {
        const auto buttons = m_overflowGroup->buttons();
        for (auto *btn : buttons) {
            btn->setEnabled(false);
        }
    }
    if (m_overflowAlarmSwitch) {
        m_overflowAlarmSwitch->setEnabled(false);
    }
}

void StorageSetting::onPathEditFinished()
{
    if (!m_pathEdit) return;

    // 取当前输入框路径与上次可用路径
    const QString newPath = m_pathEdit->text().trimmed();
    auto it = m_params.find("repo_dir");
    const QString oldPath = (it != m_params.end())
                                ? QString::fromUtf8(it->second.c_str())
                                : QString();

    // 路径没有变化则不重复下发
    if (newPath == oldPath) {
        return;
    }

    // 校验目录是否存在，不存在则重置回上次可用状态
    if (!QDir(newPath).exists()) {
        QString errInfo = QStringLiteral("目录不存在：%1，已恢复为上次可用路径 %2")
                              .arg(newPath, oldPath);
        LOG_WARN("%s", errInfo.toStdString().c_str());
        ToastWidget::instance()->showError(errInfo);
        m_pathEdit->setText(oldPath);
        return;
    }

    // 记录待确认的路径，仅在下发成功后更新到 m_params
    m_pendingPath = newPath;

    // 用最新路径构造下发参数（m_params 保持上次可用状态，避免下发失败时残留错误值）
    haiq::GuiRequestData req;
    req._reqSrc = haiq::ReqSource_Storage; // 标识请求来源，用于区分响应归属

    req._cmdType = haiq::CMDType::CMD_RESET;
    emit signalRequest(req);

    req._cmdType = haiq::CMDType::CMD_PARAM;
    auto params = m_params;
    params["repo_dir"] = newPath.toUtf8().data();
    req._paramTable["HASM_SignalRepo"] = params;
    emit signalRequest(req);
}

void StorageSetting::slotResponse(haiq::GuiResponseData& responseData)
{
    // 只处理本页（存储策略）发起的参数下发响应
    if (responseData._reqSrc != haiq::ReqSource_Storage)
        return;
    if (responseData._cmdType != haiq::CMDType::CMD_PARAM)
        return;

    if (responseData._result == haiq::Result::Success) {
        // 下发成功：更新生效参数为待确认路径
        if (!m_pendingPath.isEmpty()) {
            m_params["repo_dir"] = m_pendingPath.toStdString();
            // 通知上层（回放页面等）同步更新频谱数据存储路径
            emit signalPathChanged(m_pendingPath);
            m_pendingPath.clear();
        }
    } else {
        // 下发失败：回退到上次可用路径，参数保持不变
        auto it = m_params.find("repo_dir");
        const QString oldPath = (it != m_params.end())
                                    ? QString::fromUtf8(it->second.c_str())
                                    : QString();
        if (m_pathEdit) {
            m_pathEdit->setText(oldPath);
        }
        m_pendingPath.clear();
        ToastWidget::instance()->showError(
            QStringLiteral("存储路径下发失败，已恢复为上次可用路径 %1").arg(oldPath));
    }
}

void StorageSetting::slotListDbRecord(bool flag)
{
    if (!m_pathEdit) return;
    // 监测运行中（false）禁止修改存储路径，停止/暂停（true）恢复可编辑
    m_pathEdit->setEnabled(flag);
}
