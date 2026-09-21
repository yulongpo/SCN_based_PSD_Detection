#ifndef CONTENTPANEL_H
#define CONTENTPANEL_H

#include <QWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QEasingCurve>
#include <QResizeEvent>
#include "collMonitor/MonitorIndex.h"
#include "recordPlayback/RecordIndex.h"
#include "systemSettings/SystemSettingsIndex.h"
#include "com/common/SignalWhitelist.hpp"

class StatusBarWidget;

// 主内容面板，通过水平布局将窗口分为左右两部分。
// 左侧为 SideBar 导航侧边栏，右侧为中央内容区（含底部状态栏）。
// 页面切换带有滑动动画效果。
class ContentPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ContentPanel(QWidget *parent = nullptr);

    /** @brief 返回内容区最小宽度（逻辑像素），用于限制窗口最小拖动宽度 */
    int minimumContentWidth() const;

    /**
     * @brief 设置参数
     * @param params 参数列表
     */
    void setParams(ParamTable& params);

    /**
     * @brief 更新参数响应
     * @param responseData 响应数据
     */
    void slotResponse(haiq::GuiResponseData& responseData);

    /**
     * @brief 添加icd数据，可能是其他线程
     * @param icd 数据
     */
    void addMFICDData(const HQSigMF &icd);

signals:
    /**
     * @brief 发送请求
     * @param requestData 请求数据
     */
    void signalRequest(haiq::GuiRequestData& requestData);

    /**
     * @brief 告警规则操作请求
     * @param request 告警规则操作请求信息
     */
    void alarmRuleOpReq(const SignalAlarmRuleOpReq &request);

    /**
     * @brief 白名单操作请求
     * @param request 白名单操作请求信息
     */
    void whiteListsOpReq(const SignalWhitelistOpReq &request);

    /**
     * @brief 文件信号明细查询请求
     * @param req 请求信息
     */
    void signalDetailPerFileQueryReq(const SignalDetailPerFileQueryReq& req);

    /**
     * @brief 频谱文件删除请求
     * @param req 删除信息
     */
    void spectrumFileDelReq(const SpectrumFileDelReq& req);

    /**
     * @brief 信号删除请求
     * @param req 删除信息
     */
    void signalDelReq(const SignalDetailPerFileDelReq& req);

    /**
     * @brief 请求刷新文件列表
     */
    void requestFileListRefresh();

public slots:
    /**
     * @brief 切换到采集界面。
     */
    void collectClicked();
    /**
     * @brief 切换到录制页面。
     */
    void playbackClicked();
    /**
     * @brief 切换到系统设置页面。
     */
    void systemSettingClicked();

    /**
     * @brief 设置信号告警规则列表
     * @param rules 告警规则列表
     */
    void setSignalAlarmRules(SignalAlarmRules rules);

    /**
     * @brief 设置信号白名单列表
     * @param whiteLists 信号白名单列表
     */
    void setSignalWhiteLists(SignalWhitelists whiteLists);

    /**
     * @brief 告警规则操作响应
     * @param response 告警规则操作请求信息
     */
    void alarmRuleOpResp(const SignalAlarmRuleOpResp &response);

    /**
     * @brief 白名单操作响应
     * @param response 白名单操作请求信息
     */
    void whiteListsOpResp(const SignalWhitelistOpResp &response);

    /**
     * @brief 频谱文件信息
     * @param info 频谱文件信息
     */
    void spectrumFileInfo(const SpectrumFileInfo& info) const;

    /**
     * @brief 频谱文件信息列表
     * @param infos 频谱文件信息列表
     */
    void spectrumFileInfos(const SpectrumFileInfos& infos) const;

    /**
     * @brief 文件信号明细查询回复
     * @param resp 回复结果
     */
    void signalDetailPerFileQueryResp(const SignalDetailPerFileQueryResp& resp) const;

    /**
     * @brief 频谱文件删除回复
     * @param resp 删除回复
     */
    void spectrumFileDelResp(const SpectrumFileDelResp& resp) const;

    /**
     * @brief 信号删除回复
     * @param resp 删除回复
     */
    void signalDelResp(const SignalDetailPerFileDelResp& resp) const;

    /**
     * @brief 获取菜单参数信息
     * @param fc 中心频率
     * @param bw 扫宽
     * @param rbw 带宽分辨率
     */
    void slotMenuInfo(int64_t fc, int64_t bw, int64_t rbw);

    /**
     * @brief 文件源切换后更新状态栏
     * @param source 文件源名称（BB60C/MR60C/HarogicSAN90/FILE）
     */
    void slotSourceSwitch(const QString &source);

protected:
    /**
     * @brief 事件过滤器，监听页面容器大小变化以同步页面尺寸。
     */
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    /**
     * @brief 初始化 UI 组件。
     */
    void initUI();
    /**
     * @brief 初始化信号与槽连接。
     */
    void initConnects();
    /**
     * @brief 滑动切换到指定页面。
     * @param index 页面索引：0-采集监测，1-录制回放，2-系统设置
     */
    void slideToPage(int index);

private:
    QWidget         *m_rightPanel;
    QVBoxLayout     *m_rightLayout;
    QWidget         *m_pageContainer;          // 页面容器，用于实现滑动切换动画

    MonitorIndex    *m_collMonitorPage;        // 采集监测界面
    RecordIndex     *m_recordPlaybackPage;     // 录制回放页面
    SystemSettingsIndex *m_systemSettingsPage;  // 系统设置页面
    StatusBarWidget *m_statusBar;              // 底部状态栏
    int              m_currentPageIndex = 0;   // 当前页面索引：0-采集监测，1-录制回放，2-系统设置
    int              m_pendingIndex = -1;      // 动画期间待切换到的页面索引，-1 表示无待切换
    bool             m_animating = false;      // 动画进行中标志，防止重复触发
};

#endif // CONTENTPANEL_H
