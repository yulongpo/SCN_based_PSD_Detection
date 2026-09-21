#include "RecordFileList.h"
#include "comm/CommonMacros.h"
#include "comm/ScreenScale.h"
#include "comm/ThemeManager.h"
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>
#include <QLabel>
#include "comm/FontManager.h"

RecordFileList::RecordFileList(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, true);

    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

    const int padInt = static_cast<int>(DPR_REAL(12.0 * scale, dpr));

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(padInt, padInt, padInt, padInt);
    initUI();

}

void RecordFileList::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

    const qreal w = width();
    const qreal h = height();

    // 圆角半径 16px、边框宽度 1px（设计稿 2x 基准）
    const qreal radius  = DPR_REAL(16.0 * scale, dpr);
    const qreal borderW = DPR_REAL(1.0 * scale, dpr);
    const qreal halfBW  = borderW * 0.5;

    // 绘制圆角边框（颜色从 CollMonitor.borderColor 读取）
    QPainterPath borderPath;
    borderPath.addRoundedRect(
        QRectF(halfBW, halfBW, w - borderW, h - borderW),
        radius - halfBW, radius - halfBW);

    painter.setPen(QPen(ThemeManager::instance().color("CollMonitor.borderColor"), borderW));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(borderPath);
}

void RecordFileList::initUI()
{
    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

    auto title = new QLabel(QStringLiteral("回放文件列表"),this);
    title->setFont(FontManager::instance().font(DPR_INT(15 * scale, dpr, 0)));
    title->setStyleSheet("color: rgb(255, 255, 255);");
    m_layout->addWidget(title);
    m_layout->addSpacing(DPR_INT(12.0 * scale, dpr, 0));

    m_listBox = new HQRecordListBox(this);
    // 转发详情按钮信号
    connect(m_listBox, &HQRecordListBox::detailClicked,
            this, &RecordFileList::detailClicked);
    // 转发删除按钮信号
    connect(m_listBox, &HQRecordListBox::deleteClicked,
            this, &RecordFileList::deleteClicked);
    connect(m_listBox, &HQRecordListBox::rowDeleteClicked,
            this, &RecordFileList::rowDeleteClicked);
    connect(m_listBox, &HQRecordListBox::exportClicked,
            this, &RecordFileList::exportClicked);
    connect(m_listBox, &HQRecordListBox::dialogSignalDetailSearch,
            this, &RecordFileList::dialogSignalDetailSearch);
    connect(this, &RecordFileList::dialogSignalDetail,
            m_listBox, &HQRecordListBox::dialogSignalDetail);
    m_layout->addWidget(m_listBox, 1);

    // 无 Demo 数据，由 SpectrumFileInfos 推送
}

void RecordFileList::openRecordDialog()
{
    m_listBox->openRecordDialog();
}

void RecordFileList::setSignalPath(const QString& signalPath)
{
    m_listBox->setSignalPath(signalPath);
}
