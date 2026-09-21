#pragma once

#include <QWidget>
#include <QToolButton>
#include <QButtonGroup>
#include <QStackedWidget>
#include <QEvent>
#include <map>

#include "BaseDef.h"
#include "radioai/icd/SignalData.hpp"
#include "com/common/SignalWhitelist.hpp"

class ShowSetting;
class StorageSetting;
class AlertSetting;
class PushSetting;
class LogSetting;
class HelpSetting;

/**
 * @brief 系统设置导航按钮
 *
 * 可选中/取消选中的工具按钮，用于 SystemSettingsIndex 中的页面切换导航。
 * 选中时以 primary_bg.png 为背景绘制，未选中时使用深色背景 rgb(24,27,37)。
 */
class SettingsNavButton : public QToolButton
{
    Q_OBJECT

public:
    /**
     * @brief 构造系统设置导航按钮
     * @param text 按钮文字
     * @param parent 父控件
     */
    explicit SettingsNavButton(const QString &text, QWidget *parent = nullptr);

protected:
    /**
     * @brief 自绘按钮背景：选中用 primary_bg.png，未选中用深色填充
     * @param event 绘制事件
     */
    void paintEvent(QPaintEvent *event) override;
};

/**
 * @brief 系统设置索引面板
 *
 * 顶部为 6 个导航按钮（显示、存储策略、告警规则、推送、日志、帮助），
 * 下方为对应的内容堆栈（QStackedWidget），通过按钮互斥选择进行切换。
 * 内容堆栈占用导航栏以下的全部剩余空间。
 */
class SystemSettingsIndex : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief 构造系统设置索引面板
     * @param parent 父控件
     */
    explicit SystemSettingsIndex(QWidget *parent = nullptr);

    /** @brief 返回内容区最小宽度（逻辑像素） */
    int minimumContentWidth() const;

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
     * @brief 设置 HASM_SignalRepo 存储配置参数（展示在存储策略页面的输入框）
     * @param params[in] HASM_SignalRepo 存储配置参数表
     */
    void setSignalRepo(const std::map<std::string, std::string>& params);

signals:
    /**
     * @brief 告警规则操作请求
     * @param request 告警规则操作请求信息
     */
    void alarmRuleOpReq(const SignalAlarmRuleOpReq &request);

    /**
     * @brief 白名单操作请求
     * @param request 白名单操作请求信息
     */
    void whiteListsOpReq(const SignalWhitelistOpReq& request);

    /**
     * @brief 存储策略参数下发请求
     * @param requestData[in] 请求数据
     */
    void signalRequest(haiq::GuiRequestData& requestData);

    /**
     * @brief 存储路径下发成功（转发给上层，用于同步更新回放页面等）
     * @param newPath[in] 下发成功的新存储路径
     */
    void signalPathChanged(const QString& newPath);

public slots:
    /**
     * @brief 告警规则操作响应
     * @param response 告警规则操作请求信息
     */
    void alarmRuleOpResp(const SignalAlarmRuleOpResp &response);

    /**
     * @brief 白名单操作响应
     * @param response 白名单操作请求信息
     */
    void whiteListsOpResp(const SignalWhitelistOpResp& response);

    /**
     * @brief 存储策略参数下发响应（转发给存储设置页）
     * @param responseData[in] 响应数据
     */
    void slotResponse(haiq::GuiResponseData& responseData);

    /**
     * @brief 设置存储路径输入框的可用状态（转发给存储设置页，随监测运行状态联动）
     * @param flag[in] true-监测停止/暂停，路径可编辑；false-监测进行中，路径禁止编辑
     */
    void slotListDbRecord(bool flag);

private:
    /**
     * @brief 初始化 UI 组件
     */
    void initUI();

    /**
     * @brief 创建导航按钮
     * @param text 按钮文字
     * @param group 按钮组
     * @param id 按钮 ID
     * @return 创建的导航按钮
     */
    SettingsNavButton* createNavBtn(const QString &text, QButtonGroup *group, int id);

private:
    QWidget         *m_navWidget    = nullptr;   ///< 上方导航栏容器
    QStackedWidget  *m_contentStack = nullptr;   ///< 下方内容堆栈（占满剩余空间）

    ShowSetting     *m_showPage     = nullptr;   ///< 显示设置页面
    StorageSetting  *m_storagePage  = nullptr;   ///< 存储策略页面
    AlertSetting    *m_alertPage    = nullptr;   ///< 告警规则页面
    PushSetting     *m_pushPage     = nullptr;   ///< 推送页面
    LogSetting      *m_logPage      = nullptr;   ///< 日志页面
    HelpSetting     *m_helpPage     = nullptr;   ///< 帮助页面
};
