#include "ContentPanel.h"
#include "comm/ScreenScale.h"
#include "StatusBarWidget.h"
#include "spdlog/spdlog.h"

ContentPanel::ContentPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName("contentPanel");
    setAttribute(Qt::WA_StyledBackground, true);

    initUI();
    initConnects();
}

int ContentPanel::minimumContentWidth() const
{
    // 取所有子页面中最大的最小宽度
    int minW = 0;
    if (m_collMonitorPage) {
        minW = m_collMonitorPage->minimumContentWidth();
    }
    if (m_recordPlaybackPage) {
        minW = qMax(minW, m_recordPlaybackPage->minimumContentWidth());
    }
    if (m_systemSettingsPage) {
        minW = qMax(minW, m_systemSettingsPage->minimumContentWidth());
    }
    if (minW <= 0) {
        minW = 800;  // 安全 fallback
    }
    return minW;
}

void ContentPanel::setParams(ParamTable& params)
{
    m_collMonitorPage->setParams(params);
    // 解析出设备名称设置到状态栏 目前设备就三种：BB60C/HarogicSAN90/FILE
    for (auto it = params.begin(); it != params.end(); it++)
    {
        if (it->first == "SpectrumSource")
        {
            for (const auto& p : it->second)
            {
                if (p.first == "name")
                {
                    m_statusBar->setDeviceValue(QString::fromStdString(p.second));
                }
            }
        }
        else if (it->first == "HASM_SignalRepo")
        {
            // 将 HASM_SignalRepo 存储配置参数同步到系统设置存储策略页
            m_systemSettingsPage->setSignalRepo(it->second);
            for (const auto& p : it->second)
            {
                if (p.first == "repo_dir")
                {
                    // 将频谱数据存储路径传递给录制回放页面，供回放对话框定位频谱文件
                    m_recordPlaybackPage->setSignalPath(QString::fromStdString(p.second));
                }
            }
        }
    }
}

void ContentPanel::slotResponse(haiq::GuiResponseData& responseData)
{
    // 根据请求来源标识区分响应归属：存储策略参数下发交给系统设置页，其余交给采集监测
    if (responseData._reqSrc == haiq::ReqSource_Storage) {
        m_systemSettingsPage->slotResponse(responseData);
    } else {
        m_collMonitorPage->slotResponse(responseData);
    }
}

void ContentPanel::addMFICDData(const HQSigMF& icd)
{
    // 复制一份 HQSigMF 并转发到 CollMonitor 的处理队列
    auto copy = QSharedPointer<HQSigMF>(new HQSigMF(icd));
    m_collMonitorPage->enqueueIcdData(copy);
}

void ContentPanel::setSignalAlarmRules(SignalAlarmRules rules)
{
    m_systemSettingsPage->setSignalAlarmRules(rules);
}

void ContentPanel::setSignalWhiteLists(SignalWhitelists whiteLists)
{
    m_systemSettingsPage->setSignalWhiteLists(whiteLists);
}

void ContentPanel::alarmRuleOpResp(const SignalAlarmRuleOpResp& response)
{
    m_systemSettingsPage->alarmRuleOpResp(response);
}

void ContentPanel::whiteListsOpResp(const SignalWhitelistOpResp& response)
{
    m_systemSettingsPage->whiteListsOpResp(response);
}

void ContentPanel::spectrumFileInfo(const SpectrumFileInfo& info) const
{
    m_recordPlaybackPage->spectrumFileInfo(info);
}

void ContentPanel::spectrumFileInfos(const SpectrumFileInfos& infos) const
{
    m_recordPlaybackPage->spectrumFileInfos(infos);
}

void ContentPanel::signalDetailPerFileQueryResp(const SignalDetailPerFileQueryResp& resp) const
{
    m_recordPlaybackPage->signalDetailPerFileQueryResp(resp);
}

void ContentPanel::spectrumFileDelResp(const SpectrumFileDelResp& resp) const
{
    m_recordPlaybackPage->spectrumFileDelResp(resp);
}

void ContentPanel::signalDelResp(const SignalDetailPerFileDelResp& resp) const
{
    m_recordPlaybackPage->signalDelResp(resp);
}

void ContentPanel::slotMenuInfo(int64_t fc, int64_t bw, int64_t rbw)
{
    m_statusBar->setMenuInfo(fc, bw, rbw);
}

void ContentPanel::slotSourceSwitch(const QString &source)
{
    // 更新状态栏设备名称
    m_statusBar->setDeviceValue(source);
    // FILE 为测试数据模式，频率/扫宽/带宽分辨率标签在状态栏显示；真实设备隐藏
    m_statusBar->setMenuInfoVisible(source == QStringLiteral("FILE"));
}

void ContentPanel::collectClicked()
{
    slideToPage(0);  // 切换到首页
}

void ContentPanel::playbackClicked()
{
    slideToPage(1);  // 切换到录制回放
}

void ContentPanel::systemSettingClicked()
{
    slideToPage(2);  // 切换到系统设置
}

bool ContentPanel::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_pageContainer && event->type() == QEvent::Resize) {
        // 页面容器大小变化时，同步调整所有页面的大小（保持当前位置）
        QResizeEvent *re = static_cast<QResizeEvent *>(event);
        QSize size = re->size();
        m_collMonitorPage->resize(size);
        m_recordPlaybackPage->resize(size);
        m_systemSettingsPage->resize(size);
        return true;
    }
    return QWidget::eventFilter(obj, event);
}

void ContentPanel::initUI()
{
    const auto &ss       = ScreenScale::instance();
    const qreal dpr      = ss.dpr();
    const double scale   = ss.scale();

    // 右侧内容区容器
    m_rightPanel = new QWidget(this);
    m_rightPanel->setObjectName("contentRightPanel");
    m_rightPanel->setAttribute(Qt::WA_StyledBackground, true);

    m_rightLayout = new QVBoxLayout(m_rightPanel);
    m_rightLayout->setContentsMargins(0, 0, 0, 0);
    m_rightLayout->setSpacing(0);

    // 页面容器（不使用布局，以便手动控制位置实现滑动切换动画）
    m_pageContainer = new QWidget(m_rightPanel);
    m_pageContainer->setObjectName("pageContainer");
    // 安装事件过滤器，监听容器大小变化以同步页面尺寸
    m_pageContainer->installEventFilter(this);

    // 主页与新建页，均作为容器的子控件
    m_collMonitorPage = new MonitorIndex(m_pageContainer);
    m_recordPlaybackPage  = new RecordIndex(m_pageContainer);
    m_systemSettingsPage  = new SystemSettingsIndex(m_pageContainer);

    // 初始状态：只显示主页
    m_collMonitorPage->show();
    m_recordPlaybackPage->hide();
    m_systemSettingsPage->hide();

    m_rightLayout->addWidget(m_pageContainer, 1);   // 页面容器填满剩余空间

    // 底部状态栏
    m_statusBar = new StatusBarWidget(this);

    // 水平布局，左侧固定宽度，右侧填满剩余空间
    auto *layout = new QVBoxLayout(this);
    auto *hLayout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    hLayout->setContentsMargins(0, 0, 0, 0);
    hLayout->setSpacing(0);
    hLayout->addWidget(m_rightPanel, 1);
    layout->addLayout(hLayout);
    layout->addWidget(m_statusBar);
}

void ContentPanel::initConnects()
{
    connect(m_collMonitorPage, &MonitorIndex::signalRequest, this, &ContentPanel::signalRequest);
    connect(m_collMonitorPage, &MonitorIndex::signalMenuInfo, this, &ContentPanel::slotMenuInfo);
    // 文件源切换后同步状态栏设备名称及频率标签显隐
    connect(m_collMonitorPage, &MonitorIndex::signalSourceSwitch, this, &ContentPanel::slotSourceSwitch);
    connect(m_collMonitorPage, &MonitorIndex::detectionProgressChanged,
            m_statusBar, &StatusBarWidget::setDetectionProgress);
    connect(m_collMonitorPage, &MonitorIndex::detectionProgressCleared,
            m_statusBar, &StatusBarWidget::clearDetectionProgress);
    // 监测运行状态联动：监测进行中禁止修改存储路径输入框，停止/暂停时恢复可编辑
    connect(m_collMonitorPage, &MonitorIndex::signalListDbRecord,
            m_systemSettingsPage, &SystemSettingsIndex::slotListDbRecord);
    connect(m_collMonitorPage, &MonitorIndex::signalRowDoubleClicked, m_recordPlaybackPage, &RecordIndex::openRecordDialog);
    connect(m_systemSettingsPage, &SystemSettingsIndex::alarmRuleOpReq, this, &ContentPanel::alarmRuleOpReq);
    connect(m_systemSettingsPage, &SystemSettingsIndex::whiteListsOpReq, this, &ContentPanel::whiteListsOpReq);
    connect(m_systemSettingsPage, &SystemSettingsIndex::signalRequest, this, &ContentPanel::signalRequest);
    // 存储路径下发成功后，同步更新回放页面的频谱数据存储路径
    connect(m_systemSettingsPage, &SystemSettingsIndex::signalPathChanged,
            m_recordPlaybackPage, &RecordIndex::setSignalPath);
    connect(m_recordPlaybackPage, &RecordIndex::signalDetailPerFileQueryReq, this, &ContentPanel::signalDetailPerFileQueryReq);
    connect(m_recordPlaybackPage, &RecordIndex::spectrumFileDelReq, this, &ContentPanel::spectrumFileDelReq);
    connect(m_recordPlaybackPage, &RecordIndex::signalDelReq, this, &ContentPanel::signalDelReq);
    connect(m_recordPlaybackPage, &RecordIndex::requestFileListRefresh, this, &ContentPanel::requestFileListRefresh);
}

void ContentPanel::slideToPage(int index)
{
    // 动画进行中，记下待切换目标，等当前动画完成后再处理
    if (m_animating) {
        m_pendingIndex = index;
        return;
    }

    // 目标页面就是当前页面，不做切换
    if (index == m_currentPageIndex)
        return;

    m_animating = true;
    m_pendingIndex = -1;  // 清除之前的待切换标记

    // 根据索引映射到对应的页面控件
    auto pageForIdx = [this](int idx) -> QWidget* {
        if (idx == 0) return static_cast<QWidget *>(m_collMonitorPage);
        if (idx == 1) return static_cast<QWidget *>(m_recordPlaybackPage);
        return static_cast<QWidget *>(m_systemSettingsPage);
    };

    QWidget *currentPage = pageForIdx(m_currentPageIndex);
    QWidget *nextPage    = pageForIdx(index);

    int width = m_pageContainer->width();

    // 宽度为0（启动时尚未布局完成），直接切换跳过动画
    if (width <= 0) {
        currentPage->hide();
        nextPage->show();
        m_currentPageIndex = index;
        m_animating = false;
        return;
    }

    // 切换方向：主页→新建页 向左滑 (-1)，新建页→主页 向右滑 (+1)
    int direction = (index > m_currentPageIndex) ? -1 : 1;

    // 将新页面放在容器外起始位置，并显示（此时新旧两页同时可见）
    nextPage->setGeometry(direction * width, 0, width, m_pageContainer->height());
    nextPage->show();
    nextPage->raise();

    // 创建并行动画组：当前页滑出 & 新页面滑入
    auto *group = new QParallelAnimationGroup(this);

    // 当前页面滑出动画
    auto *slideOut = new QPropertyAnimation(currentPage, "pos");
    slideOut->setDuration(300);
    slideOut->setStartValue(QPoint(0, 0));
    slideOut->setEndValue(QPoint(-direction * width, 0));
    slideOut->setEasingCurve(QEasingCurve::InOutCubic);

    // 新页面滑入动画
    auto *slideIn = new QPropertyAnimation(nextPage, "pos");
    slideIn->setDuration(300);
    slideIn->setStartValue(QPoint(direction * width, 0));
    slideIn->setEndValue(QPoint(0, 0));
    slideIn->setEasingCurve(QEasingCurve::InOutCubic);

    group->addAnimation(slideOut);
    group->addAnimation(slideIn);

    // 动画结束后：隐藏旧页面、复位位置、更新当前索引、处理待切换
    connect(group, &QParallelAnimationGroup::finished, this, [=]() {
        currentPage->hide();
        currentPage->move(0, 0);    // 复位到容器原点
        m_currentPageIndex = index;
        m_animating = false;
        m_pageContainer->update();

        // 动画期间若有新的切换请求，继续执行
        if (m_pendingIndex >= 0 && m_pendingIndex != m_currentPageIndex) {
            int nextPending = m_pendingIndex;
            m_pendingIndex = -1;
            slideToPage(nextPending);
        }
    });

    group->start(QAbstractAnimation::DeleteWhenStopped);
}
