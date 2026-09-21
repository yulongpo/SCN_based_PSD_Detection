#include "Controller.h"
#include "radioai/ComFlow.hpp"
#include "spdlog/spdlog.h"
#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "radioai/Logger.hpp"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>

class Logger : public radioai::core::ILogger {
public:
    virtual int init()
    {
        //初始化spd log
        try {
            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            console_sink->set_level(spdlog::level::debug);
            //auto rotating_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>("logs/RadioAI_log.txt", 1024 * 1024 * 10, 3);
            //rotating_sink->set_level(spdlog::level::trace);
            //spdlog::sinks_init_list sink_list = { rotating_sink, console_sink };
            spdlog::sinks_init_list sink_list = { console_sink };
            auto logger = std::make_shared<spdlog::logger>("RadioAI", sink_list.begin(), sink_list.end());
            logger->set_level(spdlog::level::debug);
            logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%5t] %v");
            logger->flush_on(spdlog::level::debug);
            spdlog::set_default_logger(logger);

            printf(u8"日志初始化成功.\n");
            return 0;
        }
        catch (const spdlog::spdlog_ex& ex) {
            qDebug() << u8"日志初始化失败: " << ex.what();
            return -1;
        }
    }

protected:
    virtual void logImpl(radioai::core::ILogger::Level level, const char* msg) noexcept
    {
        spdlog::level::level_enum spd_level = (spdlog::level::level_enum)level;
        spdlog::log(spd_level, msg);
    }
};
radioai::core::ILogger* g_logger = nullptr;

InitThread::InitThread(Controller* controller) : m_controller(controller)
{

}

QVector<radioai::core::IComFlow*>& InitThread::getComFlows()
{
    return m_comFlows;
}

void InitThread::run()
{
    radioai::core::EComState state;
    // 初始化日志
    Logger* logger = new (std::nothrow) Logger;
    logger->setLevelThresh(radioai::core::ILogger::Level::kINFO);
    state = radioai::core::EComState::BEGIN;
    emit sendCurrentProcess(QStringLiteral("初始化系统环境"), state);
    if (0 != logger->init())
    {
        state = radioai::core::EComState::FAILED;
        emit sendCurrentProcess(QStringLiteral("初始化系统环境"), state);
        return;
    }
    g_logger = logger;

    // 构建离线检测识别流
    auto cf_spec_monitor = createComFlow("Flow_AISpecMonitor", logger);
    m_controller->regService("Flow_AISpecMonitor", cf_spec_monitor);
    if (nullptr == cf_spec_monitor)
    {
        state = radioai::core::EComState::FAILED;
        emit sendCurrentProcess(QStringLiteral("初始化系统环境"), state);
        return;
    }
    state = radioai::core::EComState::SUCCESS;
    emit sendCurrentProcess(QStringLiteral("初始化系统环境"), state);
    const QDir appDir(QCoreApplication::applicationDirPath());
    const QString flowFile = QDir::cleanPath(
        appDir.absoluteFilePath("../config/HaiAISpecMonitor/Flow_AISpecMonitor.json"));
    const QByteArray flowFileUtf8 = flowFile.toUtf8();
    if (0 != cf_spec_monitor->init(flowFileUtf8.constData()))
    {
        state = radioai::core::EComState::BEGIN;
        emit sendCurrentProcess(QStringLiteral("检查系统运行状态"), state);
        state = radioai::core::EComState::FAILED;
        emit sendCurrentProcess(QStringLiteral("检查系统运行状态"), state);
        return;
    }
    m_comFlows.append(cf_spec_monitor);

    m_controller->init();

    //启动服务列表（在线采集流仅打开设备，不启动采集线程，由 resume() 触发）
    state = radioai::core::EComState::BEGIN; // 先把这个当做 检查系统运行状态 最终阶段
    emit sendCurrentProcess(QStringLiteral("检查系统运行状态"), state);
    if (0 != cf_spec_monitor->start())
    {
        state = radioai::core::EComState::FAILED;
        emit sendCurrentProcess(QStringLiteral("检查系统运行状态"), state);
        return;
    }
    state = radioai::core::EComState::SUCCESS;
    emit sendCurrentProcess(QStringLiteral("检查系统运行状态"), state);
}

ProcessController::ProcessController(Controller* controller) : m_controller(controller), m_totalNum(6), m_currentNum(0), m_isFail(false)
{
    m_loadingDialog = new LoadingDialog();
    m_loadingDialog->show();
}

ProcessController::~ProcessController()
{
    if (m_loadingDialog != nullptr)
    {
        delete m_loadingDialog;
        m_loadingDialog = nullptr;
    }
}

void ProcessController::currentProcessChanged(QString processName, radioai::core::EComState state)
{
    if (processName == QStringLiteral("检查系统运行状态") && state == radioai::core::EComState::BEGIN)
    {
        // 当进行到检查系统运行状态就需要创建主界面了这时可以获取参数
        m_controller->initGUI();
    }

    if (state == radioai::core::EComState::SUCCESS || state == radioai::core::EComState::FAILED || state == radioai::core::EComState::TIMEOUT) m_currentNum++;
    // 显示进度
    if (m_loadingDialog != nullptr)
    {
        m_loadingDialog->setProgress(m_totalNum != m_currentNum ? (1.0f * m_currentNum / m_totalNum) * 100 : 100);
        if (processName == QStringLiteral("初始化系统环境"))
        {
            switch (state)
            {
            case radioai::core::EComState::BEGIN:
                m_loadingDialog->updateSystemStatus(TaskIconWidget::State::InProgress);
                break;
            case radioai::core::EComState::SUCCESS:
                m_loadingDialog->updateSystemStatus(TaskIconWidget::State::Completed);
                break;
            case radioai::core::EComState::FAILED:
                m_loadingDialog->updateSystemStatus(TaskIconWidget::State::Failed);
                m_isFail = true;
                break;
            }
        }
        else if (processName == QStringLiteral("初始化频谱采集功能"))
        {
            switch (state)
            {
            case radioai::core::EComState::BEGIN:
                m_loadingDialog->updateSpectrumStatus(TaskIconWidget::State::InProgress);
                break;
            case radioai::core::EComState::SUCCESS:
                m_loadingDialog->updateSpectrumStatus(TaskIconWidget::State::Completed);
                break;
            case radioai::core::EComState::FAILED:
                m_loadingDialog->updateSpectrumStatus(TaskIconWidget::State::Failed);
                m_isFail = true;
                break;
            }
        }
        else if (processName == QStringLiteral("初始化信号检测功能"))
        {
            switch (state)
            {
            case radioai::core::EComState::BEGIN:
                m_loadingDialog->updateSignalStatus(TaskIconWidget::State::InProgress);
                break;
            case radioai::core::EComState::SUCCESS:
                m_loadingDialog->updateSignalStatus(TaskIconWidget::State::Completed);
                break;
            case radioai::core::EComState::FAILED:
                m_loadingDialog->updateSignalStatus(TaskIconWidget::State::Failed);
                m_isFail = true;
                break;
            }
        }
        else if (processName == QStringLiteral("初始化告警与数据管理功能"))
        {
            switch (state)
            {
            case radioai::core::EComState::BEGIN:
                m_loadingDialog->updateAlertStatus(TaskIconWidget::State::InProgress);
                break;
            case radioai::core::EComState::SUCCESS:
                m_loadingDialog->updateAlertStatus(TaskIconWidget::State::Completed);
                break;
            case radioai::core::EComState::FAILED:
                m_loadingDialog->updateAlertStatus(TaskIconWidget::State::Failed);
                m_isFail = true;
                break;
            }
        }
        else if (processName == QStringLiteral("检查系统运行状态"))
        {
            switch (state)
            {
            case radioai::core::EComState::BEGIN:
                m_loadingDialog->updateRunningStatus(TaskIconWidget::State::InProgress);
                break;
            case radioai::core::EComState::SUCCESS:
                m_loadingDialog->updateRunningStatus(TaskIconWidget::State::Completed);
                break;
            case radioai::core::EComState::FAILED:
                m_loadingDialog->updateRunningStatus(TaskIconWidget::State::Failed);
                m_isFail = true;
                break;
            }
        }
    }

    if (m_isFail)
    {
        QTimer::singleShot(5000, [this]()
        {
            QApplication::quit();
        });
    }
    if (m_totalNum == m_currentNum)
    {
        QTimer::singleShot(1000, [this]()
        {
            if (m_loadingDialog != nullptr)
            {
                m_loadingDialog->hide();
                delete m_loadingDialog;
                m_loadingDialog = nullptr;
            }
            m_controller->showGUI();
        });
    }
}

Controller::Controller() : QObject()
{
    _isRunning = true;
    _responseCallback = nullptr;
    _radioAIGui = nullptr;
    _thread = nullptr;
}

Controller::~Controller()
{
    _isRunning = false;
    _cmdListCond.notify_all();

    if (_thread != nullptr)
    {
        _thread->join();
        delete _thread;
        _thread = nullptr;
    }
}

int Controller::init() { return 0; }

int Controller::onResult(const char* cf_name, int cf_port_id, radioai::core::ComData* data)
{
    std::string str_cf_name = cf_name;

    //检测识别流上报处理（兼容 Offline + Online）
    if ("Flow_AISpecMonitor" == str_cf_name) {
        std::lock_guard<std::mutex> guiLock(_mutexGui);
        switch (cf_port_id)
        {
        case radioai::core::RAICOM_OPORT_COM_STATUS:
            {
                // 初始化信息
                auto status = *(radioai::core::RAIComStatus*)data;
                if (strcmp(status.com_name, "SpectrumSource") == 0)
                {
                    emit sendCurrentProcess(QStringLiteral("初始化频谱采集功能"), status.phase_state);
                }
                else if (strcmp(status.com_name, "SigDetectionLargeBW") == 0)
                {
                    emit sendCurrentProcess(QStringLiteral("初始化信号检测功能"), status.phase_state);
                }
                else if (strcmp(status.com_name, "HASM_SignalAlarm") == 0 || strcmp(status.com_name, "HASM_SignalRepo") == 0)
                {
                    emit sendCurrentProcess(QStringLiteral("初始化告警与数据管理功能"), status.phase_state);
                }
                break;
            }
        case 1:
            // 信号告警规则列表SignalAlarmRules
            if (_radioAIGui != nullptr) _radioAIGui->setSignalAlarmRules(*(SignalAlarmRules*)data);
            break;
        case 2:
            if (_radioAIGui != nullptr) _radioAIGui->addICDHQSigMF(*(HQSigMF*)data);
            break;
        case 3:
            // 信号告警规则操作回复
            if (_radioAIGui != nullptr) _radioAIGui->alarmRuleOpResp(*(SignalAlarmRuleOpResp*)data);
            break;
        case 4:
            // 频谱文件信息列表
            if (_radioAIGui != nullptr) _radioAIGui->spectrumFileInfos(*(SpectrumFileInfos*)data);
            break;
        case 5:
            // 频谱文件信息
            if (_radioAIGui != nullptr) _radioAIGui->spectrumFileInfo(*(SpectrumFileInfo*)data);
            break;
        case 6:
            // 文件信号明细查询回复
            if (_radioAIGui != nullptr) _radioAIGui->signalDetailPerFileQueryResp(*(SignalDetailPerFileQueryResp*)data);
            break;
        case 7:
            // 频谱文件删除回复
            if (_radioAIGui != nullptr) _radioAIGui->spectrumFileDelResp(*(SpectrumFileDelResp*)data);
            break;
        case 8:
            // 信号删除回复
            if (_radioAIGui != nullptr) _radioAIGui->signalDelResp(*(SignalDetailPerFileDelResp*)data);
            break;
        case 9:
            // 信号白名单列表
            if (_radioAIGui != nullptr) _radioAIGui->setSignalWhiteLists(*(SignalWhitelists*)data);
            break;
        case 10:
            // 信号白名单列表操作回复
            if (_radioAIGui != nullptr) _radioAIGui->whiteListsOpResp(*(SignalWhitelistOpResp*)data);
            break;
        }
    }
    return 0;
}

void Controller::regService(std::string name, radioai::core::IComFlow* service)
{
    std::lock_guard<std::mutex> lock(_mutexIService);
    service->regObserver(this);
    _iServiceMap.insert(std::make_pair(name, service));
}

void Controller::regRespCallBack(haiq::ResponseCallback respCb)
{
    std::lock_guard<std::mutex> lock(_mutexRespCallBack);
    _responseCallback = respCb;
}

int Controller::exec(QApplication& app)
{
    // 起始时间(ms)
    //std::chrono::milliseconds start_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
    //启动异步指令下发线程
    _thread = new std::thread(&Controller::threadCallback, this);

    QCoreApplication::setOrganizationName("HaiAISpecMonitor");
    QCoreApplication::setOrganizationDomain("HaiAISpecMonitor.local");
    QCoreApplication::setApplicationName("HaiAISpecMonitorLocal");

    app.setWindowIcon(QIcon(":/title/title_log.png"));

    // 截止时间(ms)
    // std::chrono::milliseconds end_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
    // LOG_INFO("应用启动 耗时: %lld ms.", end_time - start_time);
    auto res = app.exec();

    //停止服务
    for (auto service : _iServiceMap)
    {
        service.second->stop();
    }

    // 释放资源
    {
        std::lock_guard<std::mutex> respLock(_mutexRespCallBack);
        _responseCallback = nullptr;
    }
    {
        std::lock_guard<std::mutex> guiLock(_mutexGui);
        if (_radioAIGui != nullptr)
        {
            delete _radioAIGui;
            _radioAIGui = nullptr;
        }
    }
    return res;
}

bool Controller::pushCMD(haiq::GuiRequestData& requestData)
{
    std::lock_guard<std::mutex> lock(_cmdListMutex);
    _cmdList.push_back(requestData);
    _cmdListCond.notify_all();
    return true;
}

void Controller::initGUI()
{
    if (_radioAIGui == nullptr)
    {
        _radioAIGui = new CustomWindow();
    }
    // 注册自身控制器，内部会给当前控制器注册_responseCallback回调
    _radioAIGui->registerController(this);
    auto serviceIt = _iServiceMap.find("Flow_AISpecMonitor");
    if (serviceIt != _iServiceMap.end())
    {
        ParamTable params;
        int getParamRes = serviceIt->second->getParam(params);
        if (getParamRes == 0)
        {
            _radioAIGui->setParams(params);
            LOG_INFO(u8"成功获取计算单元参数并设置")
        }
        else
        {
            LOG_ERROR(u8"获取计算单元参数失败，result: %d", getParamRes)
        }
    }
    else
    {
        LOG_ERROR(u8"未注册服务IService=%s，无法获取计算单元参数!", "Flow_DetRecog_Offline")
    }
}

void Controller::showGUI()
{
    // 初始化完成后显示主窗口，并以最大化方式呈现
    _radioAIGui->showMaximized();
    _radioAIGui->activateWindow();
}

bool Controller::filterOperation(const haiq::GuiRequestData& requestData, radioai::core::IComFlow* service, haiq::GuiResponseData& resp)
{
    LOG_INFO(u8"未实现extraOperation额外操作函数");
    return false;
}

radioai::core::IComFlow* Controller::getComflowPtr(std::string name)
{
    auto serviceIt = _iServiceMap.find(name);
    if (serviceIt != _iServiceMap.end())
    {
        return serviceIt->second;
    }

    return nullptr;
}

void Controller::threadCallback()
{
    std::unique_lock<std::mutex> lock(_cmdListMutex);
    while (_isRunning)
    {
        if (_cmdList.empty())
        {
            _cmdListCond.wait_for(lock, std::chrono::milliseconds(1000));
        }
        while (_isRunning && !_cmdList.empty())
        {
            std::lock_guard<std::mutex> serviceLock(_mutexIService);
            auto item = _cmdList.front();

            // 根据数据源类型选择目标流
            auto serviceIt = _iServiceMap.find("Flow_AISpecMonitor");
            if (serviceIt != _iServiceMap.end())
            {
                haiq::GuiResponseData resp{item._cmdType, haiq::Result::Success, item._uuid};
                resp._reqSrc = item._reqSrc; // 回填请求来源，用于区分响应归属
                if (!filterOperation(item, serviceIt->second, resp))
                {
                    switch (item._cmdType)
                    {
                    case haiq::CMDType::CMD_START:
                        if (serviceIt->second->start())
                        {
                            resp._result = haiq::Result::Fail;
                        }
                        break;
                    case haiq::CMDType::CMD_PAUSE:
                        serviceIt->second->pause();
                        break;
                    case haiq::CMDType::CMD_RESUME:
                        serviceIt->second->reset();
                        serviceIt->second->resume();
                        break;
                    case haiq::CMDType::CMD_STOP:
                        serviceIt->second->pause();
                        //serviceIt->second->stop();
                        break;
                    case haiq::CMDType::CMD_RESET:
                        serviceIt->second->reset();
                        break;
                    case haiq::CMDType::CMD_PARAM:
                        if (serviceIt->second->setParam(item._paramTable))
                        {
                            resp._result = haiq::Result::Fail;
                        }
                        break;
                    case haiq::CMDType::CMD_RULE_OPER_REQ:
                        {
                            SignalAlarmRuleOpReq* req = new SignalAlarmRuleOpReq();
                            *req = *static_cast<SignalAlarmRuleOpReq*>(item._comData.get());
                            if (serviceIt->second->process(1, req))
                            {
                                resp._result = haiq::Result::Fail;
                            }
                        }
                        break;
                    case haiq::CMDType::CMD_FILE_SIG_SEARCH_REQ:
                        {
                            SignalDetailPerFileQueryReq* req = new SignalDetailPerFileQueryReq();
                            *req = *static_cast<SignalDetailPerFileQueryReq*>(item._comData.get());
                            if (serviceIt->second->process(2, req))
                            {
                                resp._result = haiq::Result::Fail;
                            }
                        }
                        break;
                    case haiq::CMDType::CMD_FREQ_FILE_DEL_REQ:
                        {
                            SpectrumFileDelReq* req = new SpectrumFileDelReq();
                            *req = *static_cast<SpectrumFileDelReq*>(item._comData.get());
                            if (serviceIt->second->process(3, req))
                            {
                                resp._result = haiq::Result::Fail;
                            }
                        }
                        break;
                    case haiq::CMDType::CMD_SIGNAL_DEL_REQ:
                        {
                            SignalDetailPerFileDelReq* req = new SignalDetailPerFileDelReq();
                            *req = *static_cast<SignalDetailPerFileDelReq*>(item._comData.get());
                            if (serviceIt->second->process(4, req))
                            {
                                resp._result = haiq::Result::Fail;
                            }
                        }
                        break;
                    case haiq::CMDType::CMD_REFRESH_FILE_LIST:
                        serviceIt->second->reset();
                        break;
                    case haiq::CMDType::CMD_WHITE_OPER_REQ:
                        {
                            SignalWhitelistOpReq* req = new SignalWhitelistOpReq();
                            *req = *static_cast<SignalWhitelistOpReq*>(item._comData.get());
                            if (serviceIt->second->process(5, req))
                            {
                                resp._result = haiq::Result::Fail;
                            }
                        }
                        break;
                    default:
                        LOG_WARN(u8"未知命令: {0}", (int)item._cmdType);
                        resp._result = haiq::Result::Fail;
                        break;
                    }
                }
                std::lock_guard<std::mutex> respLock(_mutexRespCallBack);
                if (_responseCallback != nullptr) _responseCallback(resp);
            }
            else
            {
                LOG_ERROR(u8"未注册服务IService=%s，调用命令:{%d} 失败!", "Flow_AISpecMonitor", (int)_cmdList.front()._cmdType)
                haiq::GuiResponseData resp{_cmdList.front()._cmdType, haiq::Result::Fail};
                resp._reqSrc = _cmdList.front()._reqSrc; // 回填请求来源，用于区分响应归属
                std::lock_guard<std::mutex> respLock(_mutexRespCallBack);
                if (_responseCallback != nullptr)
                {
                    _responseCallback(resp);
                }
            }
            _cmdList.pop_front();
        }
    }
}
