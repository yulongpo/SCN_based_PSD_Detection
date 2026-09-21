#pragma once

#include "radioai/Com.hpp"
#include "radioai/ComFlowObserver.hpp"
#include "radioai/Logger.hpp"
#include <map>
#include <string>

namespace radioai { namespace core{

/**
 * @brief 组件式应用的顶层执行容器与环境。
 *
 * 实现负责装载组件工厂、建立组件流连接、转发参数和驱动生命周期；具体线程
 * 调度和数据所有权由实现类与 Com/ComData 契约共同决定。
 */
class IComFlow {
public:
    IComFlow(const char* name, ILogger* logger){}
	virtual ~IComFlow() = default;

    /// 组件实例名 → 参数名和值；setParam 通常按实例粒度进行原子更新。
    typedef std::map<std::string, std::map<std::string, std::string> > ParamTable;

    /**
     * @brief 初始化
     * @param cfg_path 配置文件路径
     * @return 0 - 成功 其它失败
     */
    virtual int init(const char* cfg_path) = 0;

    /**
     * @brief 启动
     * @return 0 - 成功 其它失败
     */
    virtual int start() = 0;

    /**
     * @brief 暂停
     */
    virtual void pause() = 0;

    /**
     * @brief 恢复
     */
    virtual void resume() = 0;

    /**
     * @brief 重置
     * @return 0-成功 非0-失败
     */
    virtual int reset() = 0;

    /**
     * @brief 停止
     */
    virtual void stop() = 0;

    /**
     * @brief 注入组件流观察者
     */
    virtual void regObserver(IComFlowObserver* observer) = 0;

    /**
     * @brief 设置参数
     * @param param_table：要更新的参数表
     * @reutrn 0 - 成功，其它失败
     */
    virtual int setParam(const ParamTable& param_table) = 0;

    /**
     * @brief 获取参数
     * @param param_table：参数列表
     * @reutrn 0 - 成功，其它失败
     */
    virtual int getParam(ParamTable& param_table) = 0;

    /**
     * @brief 查询设备可用状态
     * @param[out] results 设备类型 → 可用性 (true=可用)
     * @return 0-成功，-1-不支持
     */
    virtual int getDeviceStatus(std::map<std::string, bool>& results) { return -1; }

    /**
     * @brief 处理
     * @param cf_port_id 端口序号
     * @param data 
     * @reutrn 0 - 成功，其它失败
     */
    /// 接收外部或观察者投递的数据，交由指定 Flow 端口处理。
    virtual int process(int cf_port_id, ComData* data) = 0;
};

}}

// 核心库对外导出的 Flow 工厂；与 RAICOM_EXPORT 的组件工厂保持相同 C ABI。
extern "C" RAICORE_API radioai::core::IComFlow * createComFlow(const char* name, radioai::core::ILogger * logger);
extern "C" RAICORE_API void destoryComFlow(radioai::core::IComFlow * obj);
