#include "MonitorIndex.h"
#include "comm/CommonMacros.h"
#include "comm/ScreenScale.h"
#include "comm/ThemeManager.h"
#include <QPainter>
#include <QPainterPath>
#include <QtMath>
#include <QVBoxLayout>

MonitorIndex::MonitorIndex(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, true);

    initUI();
}

void MonitorIndex::initUI()
{
    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

    // 内边距 12px（设计稿 2x 基准），转换为逻辑像素后保存
    const int padInt = static_cast<int>(DPR_REAL(12.0 * scale, dpr));

    auto contentLayout = new QVBoxLayout(this);
    // 设置内边距，使子控件不会贴到自绘边框的边缘
    contentLayout->setContentsMargins(padInt, padInt, padInt, padInt);

    // --- 创建时频瀑布图 ---
    m_collMonitorMenu = new CollMonitorMenu(this);
    m_collMonitor = new CollMonitor(this);
    contentLayout->addWidget(m_collMonitorMenu);
    contentLayout->addWidget(m_collMonitor);

    connect(m_collMonitorMenu, &CollMonitorMenu::signalRequest, this, &MonitorIndex::signalRequest);
    connect(m_collMonitorMenu, &CollMonitorMenu::signalParams, m_collMonitor, &CollMonitor::slotParams);
    connect(m_collMonitorMenu, &CollMonitorMenu::signalIsUpdateData, m_collMonitor, &CollMonitor::slotIsUpdateData);
    connect(m_collMonitorMenu, &CollMonitorMenu::signalClearData, m_collMonitor, &CollMonitor::slotClearData);
    connect(m_collMonitorMenu, &CollMonitorMenu::signalListDbRecord, m_collMonitor, &CollMonitor::slotListDbRecord);
    // 转发监测运行状态，供上层（系统设置存储路径输入框等）联动
    connect(m_collMonitorMenu, &CollMonitorMenu::signalListDbRecord, this, &MonitorIndex::signalListDbRecord);
    connect(m_collMonitorMenu, &CollMonitorMenu::signalMenuInfo, this, &MonitorIndex::signalMenuInfo);
    connect(m_collMonitorMenu, &CollMonitorMenu::signalSourceSwitch, this, &MonitorIndex::signalSourceSwitch);
    connect(m_collMonitor, &CollMonitor::addSignalNum, m_collMonitorMenu, &CollMonitorMenu::addSignalNum);
    connect(m_collMonitor, &CollMonitor::signalRowDoubleClicked, this, &MonitorIndex::signalRowDoubleClicked);
    connect(m_collMonitor, &CollMonitor::detectionProgressChanged,
            this, &MonitorIndex::detectionProgressChanged);
    connect(m_collMonitor, &CollMonitor::detectionProgressCleared,
            this, &MonitorIndex::detectionProgressCleared);
}

int MonitorIndex::minimumContentWidth() const
{
    // 基于 CollMonitorMenu 的布局计算最小宽度
    // 子控件均已设置 minimumWidth / fixedSize，Qt 布局可正确计算 minimumSizeHint
    if (m_collMonitorMenu) {
        int w = m_collMonitorMenu->minimumSizeHint().width() + DPR_INT(30 * ScreenScale::instance().scale(), ScreenScale::instance().dpr(), 0);
        if (w > 0) {
            return w;
        }
    }
    // 布局尚未完成时的 fallback 值（逻辑像素）
    const auto &ss = ScreenScale::instance();
    return static_cast<int>(DPR_REAL(1480.0 * ss.scale(), ss.dpr()));
}

void MonitorIndex::setParams(ParamTable& params)
{
    m_collMonitorMenu->setParams(params);
}

void MonitorIndex::slotResponse(haiq::GuiResponseData& responseData)
{
    m_collMonitorMenu->slotResponse(responseData);
}

void MonitorIndex::enqueueIcdData(QSharedPointer<HQSigMF> data)
{
    m_collMonitor->enqueueIcdData(data);
}
