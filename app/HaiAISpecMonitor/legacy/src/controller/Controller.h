#pragma once
#include "../StdAfx.h"
#include <QApplication>
#include <QIcon>
#include <QThread>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <atomic>
#include "customwindow.h"
#include "initialization.h"
#include "radioai/ComStatus.hpp"
#include "com/common/SignalWhitelist.hpp"

/**
 * @brief 初始化线程，当所有内容初始化完成时退出
 */
class InitThread : public QThread
{
    Q_OBJECT
public:
    InitThread(Controller* controller);

    QVector<radioai::core::IComFlow*>& getComFlows();

signals:
    void sendCurrentProcess(QString processName, radioai::core::EComState state);

protected:
    void run() override;

private:
    Controller* m_controller;
    QVector<radioai::core::IComFlow*> m_comFlows;
};

/**
 * @brief 进度控制器
 */
class ProcessController : public QObject
{
    Q_OBJECT
public:
    ProcessController(Controller* controller);
    ~ProcessController();
public slots:
    /**
     * @brief           界面显示--------------------------------实际内容 \n
     *                  初始化系统环境---------------------------读取配置、初始化日志、加载界面资源、创建主界面 \n
     *                  初始化频谱采集功能------------------------加载BB60C SDK、初始化设备管理、搜索并连接设备 \n
     *                  初始化信号检测功能------------------------初始化信号检测组件、加载检测参数或算法模型 \n
     *                  初始化告警与数据管理功能-------------------初始化告警组件、信号仓库组件、数据读写服务 \n
     *                  检查系统运行状态-------------------------检查关键功能、设备状态、数据链路和异常信息
     * @param processName 具体名称，由上方定义
     */
    void currentProcessChanged(QString processName, radioai::core::EComState state);
private:
    Controller* m_controller;
    LoadingDialog* m_loadingDialog;

    int m_totalNum;             // 初始化的总数  初始化告警与数据管理功能是存在两个所以是6
    int m_currentNum;           // 已初始化的数量
    bool m_isFail;              // 是否存在初始化失败的项
};

class Controller : public QObject, public radioai::core::IComFlowObserver {
Q_OBJECT
public:
    Controller();

    /**
     * @brief 析构函数
     */
    virtual ~Controller();

    virtual int init();

    /**
     * @brief 结果上报
     * @param cf_name 组件流名称
     * @param cf_port_id 组件流端口序号
     * @param data 端口数据
     * @reutrn 0 - 成功，其它失败
     */
    virtual int onResult(const char* cf_name, int cf_port_id, radioai::core::ComData* data) override;

    /**
     * @brief 注入底层服务
     * @param[in] name:流名称
     * @param[in] service:流对象
     */
    void regService(std::string name, radioai::core::IComFlow* service);

    /**
     * @brief 注入响应回调
     */
    void regRespCallBack(haiq::ResponseCallback respCb);

    /**
     * @brief GUI线程启动器
     * @return QApplication
     */
    int exec(QApplication& app);

    /**
     * @brief 添加指令GUI
     * @param requestData 指令与数据
     */
    bool pushCMD(haiq::GuiRequestData& requestData);

    /**
     * @brief 初始化GUI界面
     */
    void initGUI();

    /**
     * @brief 显示界面
     */
    void showGUI();

signals:
    /**
     * @brief 通知加载界面进度信息
     * @param processName 组件名称
     * @param state 状态
     */
    void sendCurrentProcess(QString processName, radioai::core::EComState state);

protected:
    /**
     * @brief 调用命令的额外操作
     * @param requestData 请求数据
     * @param service 服务对象
     * @param resp 操作结果
     * @return 是否已操作 true:是
     */
    virtual bool filterOperation(const haiq::GuiRequestData& requestData, radioai::core::IComFlow* service, haiq::GuiResponseData& resp);

protected:
    /**
     * @brief 获取业务流对象
     * @param name 业务流名称
     * @return 业务流指针对象
     */
    radioai::core::IComFlow* getComflowPtr(std::string name);

private:
    /**
     * @brief 数据发送线程
     */
    void threadCallback();

private:
    CustomWindow* _radioAIGui;            // GUI窗口
    std::mutex _mutexGui;               // GUI窗口锁
    std::atomic<bool> _isRunning;       // 运行标志
    std::map<std::string, radioai::core::IComFlow*> _iServiceMap;// 多业务流
    std::mutex _mutexIService;          // 服务对象锁

    std::list<haiq::GuiRequestData> _cmdList;   //指令列表
    std::mutex _cmdListMutex;                   //指令锁
    std::condition_variable _cmdListCond;       //指令条件变量

    haiq::ResponseCallback _responseCallback;   //GUI响应回调
    std::mutex _mutexRespCallBack;              //GUI响应回调锁

    std::thread* _thread = nullptr;
};
