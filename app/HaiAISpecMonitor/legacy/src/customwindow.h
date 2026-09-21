#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QColor>
#include <QApplication>
#include <QString>
#include "StdAfx.h"
#include "widgets/ToastWidget.h"
#include "com/common/SignalWhitelist.hpp"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

class CustomTitleBar;
class ContentPanel;
class Controller;

class CustomWindow : public QWidget
{
    Q_OBJECT

public:
    explicit CustomWindow(QWidget *parent = nullptr);
    ~CustomWindow() override;

    int titleBarHeight() const;

    /**
     * @brief 设置参数
     * @param params 参数列表
     */
    void setParams(ParamTable& params);

    /**
     * @brief 注册控制器
     * @param controller 控制器
     */
    void registerController(Controller* controller);

    /**
     * @brief 添加ICD数据 !!!可能是其他线程调用!!!
     * @param icd icd数据
     */
    void addICDHQSigMF(const HQSigMF &icd);

    /**
     * @brief 设置信号告警规则列表
     * @param rules 告警规则列表
     */
    void setSignalAlarmRules(const SignalAlarmRules &rules);

    /**
     * @brief 设置信号白名单列表
     * @param whiteLists 信号白名单列表
     */
    void setSignalWhiteLists(const SignalWhitelists &whiteLists);

signals:
    void windowStateChanged(Qt::WindowStates state);

    /**
     * @brief 发送响应数据到GUI线程
     * @param responseData 响应数据
     */
    void signalRespToGui(haiq::GuiResponseData responseData);

    /**
     * @brief 设置信号告警规则列表
     * @param rules 告警规则列表
     */
    void signalAlarmRules(SignalAlarmRules rules);

    /**
     * @brief 设置信号告警规则列表
     * @param whiteLists 信号白名单列表
     */
    void signalWhiteLists(SignalWhitelists whiteLists);

    /**
     * @brief 告警规则操作响应
     * @param response 告警规则操作请求信息
     */
    void signalAlarmRuleOpResp(SignalAlarmRuleOpResp response);

    /**
     * @brief 白名单操作响应
     * @param response 白名单操作请求信息
     */
    void signalWhiteListsOpResp(SignalWhitelistOpResp response);

    /**
     * @brief 频谱文件信息
     * @param info 频谱文件信息
     */
    void signalSpectrumFileInfo(SpectrumFileInfo info);

    /**
     * @brief 频谱文件信息列表
     * @param infos 频谱文件信息列表
     */
    void signalSpectrumFileInfos(SpectrumFileInfos infos);

    /**
     * @brief 文件信号明细查询回复
     * @param resp 回复结果
     */
    void signalSignalDetailPerFileQueryResp(SignalDetailPerFileQueryResp resp);

    /**
     * @brief 频谱文件删除回复
     * @param resp 删除回复
     */
    void signalSpectrumFileDelResp(SpectrumFileDelResp resp);

    /**
     * @brief 信号删除回复
     * @param resp 删除回复
     */
    void signalSignalDetailPerFileDelResp(SignalDetailPerFileDelResp resp);

public slots:
    /**
     * @brief 向控制器发送请求
     * @param requestData 请求数据
     */
    void slotRequest(haiq::GuiRequestData& requestData);

    /**
     * @brief 得到响应数据
     * @param responseData 响应数据
     */
    void slotRespToGui(haiq::GuiResponseData responseData);

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
    void spectrumFileInfo(const SpectrumFileInfo& info);

    /**
     * @brief 频谱文件信息列表
     * @param infos 频谱文件信息列表
     */
    void spectrumFileInfos(const SpectrumFileInfos& infos);

    /**
     * @brief 文件信号明细查询回复
     * @param resp 回复结果
     */
    void signalDetailPerFileQueryResp(const SignalDetailPerFileQueryResp& resp);

    /**
     * @brief 频谱文件删除回复
     * @param resp 删除回复
     */
    void spectrumFileDelResp(const SpectrumFileDelResp& resp);

    /**
     * @brief 信号删除回复
     * @param resp 删除回复
     */
    void signalDelResp(const SignalDetailPerFileDelResp& resp);

protected:
    bool nativeEvent(const QByteArray &eventType, void *message, long *result) override;
    void changeEvent(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void loadThemeStyleSheets();
    void applyTheme(bool night);
    void updateBorderColor();
    /**
     * @brief 打印日志
     * @param result 结果
     * @param cmdType 命令类型
     */
    void printLog(haiq::Result result, haiq::CMDType cmdType);

    /**
     * @brief 控制器响应回调 !!!可能是其他线程调用!!!
     * @param data 响应数据
     */
    void responseCallBack(haiq::GuiResponseData& data);

#ifdef Q_OS_WIN
    static int getWindowDpi(HWND hwnd);
    static int topResizeHandleHeight(HWND hwnd);
    bool isInTitleBar(const LPARAM lParam) const;
#endif

    CustomTitleBar *m_titleBar;
    ContentPanel *m_contentPanel;
    QVBoxLayout *m_layout;
    bool m_isMaximized;
    bool m_isNightMode;
    QColor m_windowBackgroundColor;
    QColor m_inactiveBorderColor;
    QString m_styleSheetTemplate;

    Controller* m_controller;     // 外部调用控制器
};
