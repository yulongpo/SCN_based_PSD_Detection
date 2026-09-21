#include "HQDetailInfo.h"
#include "widgets/HQToolButton.h"
#include "comm/CommonMacros.h"
#include "comm/ScreenScale.h"
#include "comm/ThemeManager.h"
#include "comm/FontManager.h"
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>

HQDetailInfo::HQDetailInfo(const QStringList &rowData, QWidget *parent)
    : QWidget(parent)
    , m_rowData(rowData)
{
    setAttribute(Qt::WA_StyledBackground, true);
    initUI();
    updateContent();
}

void HQDetailInfo::setData(const QStringList &rowData)
{
    m_rowData = rowData;
    updateContent();
}

void HQDetailInfo::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

    const qreal w       = width();
    const qreal h       = height();
    const qreal radius  = DPR_REAL(16.0 * scale, dpr);
    const qreal borderW = DPR_REAL(1.0 * scale, dpr);
    const qreal halfBW  = borderW * 0.5;

    // 圆角边框
    QPainterPath borderPath;
    borderPath.addRoundedRect(
        QRectF(halfBW, halfBW, w - borderW, h - borderW),
        radius - halfBW, radius - halfBW);

    painter.setPen(QPen(ThemeManager::instance().color("CollMonitor.borderColor"), borderW));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(borderPath);

    // 工具栏与内容之间的分割线（横贯全宽，不受布局边距影响）
    if (m_toolbar) {
        const int lineY = m_toolbar->y() + m_toolbar->height()
                          + (m_layout ? m_layout->spacing() / 2 : 0);
        painter.fillRect(0, lineY - 1, w, 1, QColor(255, 255, 255, 30));
    }
}

// 字段索引表：data[idx] 对应 HQRecordListBox 的 ColIndex(1)~ColAlertNum(12)
static const int kGridDataIdx[2][4] = {
    { 2, 3, 4, 5 },   // 采集设备, 起始频率, 结束频率, RBW
    { 6, 7, 8, 9 },   // 开始时间, 结束时间, 总时长, 大小
};

void HQDetailInfo::initUI()
{
    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

    const int padInt = static_cast<int>(DPR_REAL(12.0 * scale, dpr));
    const int spacing = DPR_INT(20 * scale, dpr, 10);

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(padInt, padInt, padInt, padInt);
    m_layout->setSpacing(spacing);

    // ============================================================
    // 1. 顶部工具栏（只创建一次）
    // ============================================================
    auto *toolbar = new QWidget(this);
    auto *toolLay = new QHBoxLayout(toolbar);
    toolLay->setContentsMargins(0, 0, 0, 0);
    toolLay->setSpacing(DPR_INT(12 * scale, dpr, 6));

    const int toolBtnH = DPR_INT(28 * scale, dpr, 22);
    const int toolBtnW = DPR_INT(79 * scale, dpr, 79);

    m_toolbar = toolbar;

    struct { const char *icon; const char *text; void (HQDetailInfo::*signal)(); bool red; } btnDefs[] = {
        { "back.png",   u8"返回",   &HQDetailInfo::backClicked,   false },
    };
    for (int i = 0; i < 1; ++i) {
        const auto &def = btnDefs[i];
        auto *btn = new HQToolButton(toolbar);
        btn->setHQIcon(QIcon(QStringLiteral(":/recordplayback/%1").arg(QString::fromLatin1(def.icon))), 12, 12);
        btn->setText(QString::fromUtf8(def.text));
        btn->setFixedSize(toolBtnW, toolBtnH);
        btn->setHQRadius(6);
        btn->setTextColor(def.red ? QColor(230, 62, 62) : QColor(255, 255, 255));
        connect(btn, &QPushButton::clicked, this, def.signal);
        toolLay->addWidget(btn);
        if (i == 0)
            toolLay->addStretch();
    }

    m_layout->addWidget(toolbar);

    // ============================================================
    // 2. 内容区：文件名标题 + Grid 详情
    // ============================================================
    m_titleLabel = new QLabel(this);
    m_titleLabel->setFont(FontManager::instance().font(DPR_INT(15 * scale, dpr, 0), QFont::Bold));
    m_titleLabel->setStyleSheet("color: rgb(255, 255, 255);");
    m_layout->addWidget(m_titleLabel);

    m_layout->addSpacing(DPR_INT(8 * scale, dpr, 4));

    // Grid 布局：2 行 4 列，每格内字段名在左、值在右
    auto *gridWidget = new QWidget(this);
    auto *gridLayout = new QGridLayout(gridWidget);
    gridLayout->setContentsMargins(0, 0, 0, 0);
    gridLayout->setHorizontalSpacing(DPR_INT(24 * scale, dpr, 12));
    gridLayout->setVerticalSpacing(DPR_INT(16 * scale, dpr, 8));

    static const char *kFieldNames[2][4] = {
        { u8"采集设备:", u8"起始频率:", u8"结束频率:", "RBW:" },
        { u8"开始时间:", u8"结束时间:", u8"总时长:",   u8"大小:" },
    };

    const int cellFs = DPR_INT(13 * scale, dpr, 0);

    for (int r = 0; r < 2; ++r) {
        for (int c = 0; c < 4; ++c) {
            // 每格：字段名 | 值（水平排列）
            auto *cell = new QWidget(gridWidget);
            auto *hLay = new QHBoxLayout(cell);
            hLay->setContentsMargins(0, 0, 0, 0);
            hLay->setSpacing(DPR_INT(8 * scale, dpr, 4));

            auto *fieldLbl = new QLabel(QString::fromUtf8(kFieldNames[r][c]), cell);
            fieldLbl->setFont(FontManager::instance().font(cellFs));
            fieldLbl->setStyleSheet("color: rgba(255,255,255,0.5);");
            fieldLbl->setFixedWidth(DPR_INT(64 * scale, dpr, 40));

            m_gridValues[r][c] = new QLabel(QString(), cell);
            m_gridValues[r][c]->setFont(FontManager::instance().font(cellFs));
            m_gridValues[r][c]->setStyleSheet("color: rgb(255,255,255);");

            hLay->addWidget(fieldLbl, 0, Qt::AlignVCenter);
            hLay->addWidget(m_gridValues[r][c], 1, Qt::AlignVCenter);

            gridLayout->addWidget(cell, r, c);
        }
    }
    for (int c = 0; c < 4; ++c)
        gridLayout->setColumnStretch(c, 1);

    m_layout->addWidget(gridWidget);

    // 剩余空间填充
    m_layout->addStretch();
}

void HQDetailInfo::updateContent()
{
    // 更新标题（文件名）
    QString fileName;
    if (m_rowData.size() > 1)
        fileName = m_rowData.at(1);
    m_titleLabel->setText(fileName.isEmpty() ? QStringLiteral("文件信息") : fileName);

    // 更新 Grid 值
    for (int r = 0; r < 2; ++r) {
        for (int c = 0; c < 4; ++c) {
            const int idx = kGridDataIdx[r][c];
            QString val;
            if (idx < m_rowData.size())
                val = m_rowData.at(idx);
            // 为频率 / RBW 列添加单位
            if (idx == 3 || idx == 4) {
                if (!val.isEmpty() && val != QStringLiteral("-"))
                    val += QStringLiteral(" MHz");
            } else if (idx == 5) {
                if (!val.isEmpty() && val != QStringLiteral("-"))
                    val += QStringLiteral(" kHz");
            }
            m_gridValues[r][c]->setText(val.isEmpty() ? QStringLiteral("-") : val);
        }
    }
}
