#pragma once

#include "radioai/ComData.hpp"
#include "radioai/ComStatus.hpp"
#include "radioai/Logger.hpp"
#include <map>                  // !!! 后续考虑ABI兼容性，会去掉
#include <string>

namespace radioai { namespace core{

/**
 * @brief 组件流式数据输出回调。
 *
 * 回调接收组件输出的裸 ComData 指针；所有权由调用链约定，回调若要异步使用
 * 数据必须在返回前完成克隆或转移到带生命周期管理的对象中。
 * @param[in] int 端口号
 * @param[in] ComData* 数据对象
 * @param[in] user_data* 用户自定义数据
 * @return 0-成功  非0-失败
 */
typedef int (*ComOutputCallBack)(int, ComData*, void*);

/**
 * @brief RadioAI 组件生命周期和数据处理基类。
 *
 * 运行时通过 RAICOM_EXPORT 生成的 C ABI 工厂创建派生类，然后按 init→start→
 * process/pause/resume/reset→stop 的生命周期调用。默认实现是兼容空组件的桩，
 * 生产组件应覆盖与自身能力对应的接口。
 */
class Com
{
public:
    virtual ~Com() = default;

    /**
     * @brief 组件初始化接口
     * @return 操作是否成功。0成功，其它表示失败
     */
    /// 分配算法、池和外部资源；成功后才允许 start/process。
    virtual int init() { return 0; }

    /**
     * @brief 组件启动接口
     * @return 操作是否成功。0成功，其它表示失败
     */
    /// 进入运行态；组件可在这里启动线程或设备采集。
    virtual int start() { return 0; }

    /**
     * @brief 组件暂停接口
     * @return 无
     */
    /// 暂停接收或处理新数据，资源通常仍保持有效。
    virtual void pause(){}

    /**
     * @brief 组件暂停恢复接口
     * @return 无
     */
    /// 从 pause 状态恢复调度。
    virtual void resume() {}

    /**
     * @brief 组件重置接口
     * @return 0-成功 非0-失败
     */
    /// 清空运行历史而保留配置，具体是否重建资源由组件定义。
    virtual int reset() { return 0; }

    /**
     * @brief 组件停止接口
     */
    /// 停止运行并释放线程/设备使用权；对象仍可按框架需要析构。
    virtual void stop(){}

    /**
     * @brief 组件参数检查接口
     * @param[in] params 参数列表 
     * @return 操作是否成功。0成功，其它表示失败
     */
    /// 在 updateParam 前校验外部字符串参数，不应改变当前运行状态。
    virtual int checkParam(const std::map<std::string, std::string>& params) { return 0; }

    /**
     * @brief 组件参数更新接口
     * @param[in] params 参数列表
     * @return 操作是否成功。0成功，其它表示失败
     */
    /// 应用已校验参数；复杂组件应采用失败回滚或事务式替换。
    virtual int updateParam(const std::map<std::string, std::string>& params) { return 0; }

    /**
     * @brief 查询设备可用状态（用于 CMD_DEVICE_SCAN）
     * @param[out] results 设备类型 → 可用性 (true=可用)
     * @return 0-成功，-1-不支持
     */
    /// 查询组件依赖的 CPU/GPU/采集设备；默认表示组件不支持查询。
    virtual int getDeviceStatus(std::map<std::string, bool>& results) { return -1; }

    /**
     * @brief 是否可以处理
     * @param[in] portId 输入端口
     * @return true 可以处理，false 不可以
     */
    /// 调度器在投递前调用；组件可用它反映池耗尽、设备忙或输入端口不匹配。
    virtual bool canProcess(int port_id) { return true; }

    /**
     * @brief 数据处理函数
     * @param[in] portId 输入端口
     * @param[in] data 输入的数据
     * @return 操作是否成功。0成功，其它表示失败
     */
    /// 同步处理一条输入；成功后通常通过 output() 把结果送到下游队列。
    virtual int process(int port_id, ComData* data) { return 0; }

    /**
     * 设置输出回调。回调指针和 user_data 由调用方保证在组件使用期间有效。
     * @param cb 流式数据输出回调
     * @param user_data 用户自定义数据
     */
    void setOutputCallBack(ComOutputCallBack cb, void* user_data)
    {
        _out_cb = cb;
        _user_data = user_data;
    }

    /**
     * 设置实例名称。名称会被状态上报和日志使用。
     * @param name 实例名称
     */
    void setName(const char* name){_name = name;}

    /**
     * 设置日志后端；组件只保存裸指针，不负责释放 logger。
     * @param name 实例名称
     */
    void setLogger(ILogger* logger) { _logger = logger; }
protected:
    /**
     * @brief 从指定的输出端口输出数据。
     * 未设置回调时视为丢弃并返回成功，这是兼容离线单元测试的历史行为。
     * @param[in] port_id 输出端口
     * @param[in] data 输出的数据
     * @return 操作是否成功。0成功，其它表示失败
     */
    int output(int port_id, ComData* data)
    {
        if(nullptr != _out_cb)
        {
            return _out_cb(port_id, data, _user_data);
        }
        return 0;
    }

    /**
    * @brief 组件状态上报接口。
    * 函数会分配一个 RAIComStatus，并把它交给状态端口回调；回调失败时由调用方
    * 负责理解返回码，函数本身不会自动重试。
    * @param[in] phase 生命周期阶段
    * @param[in] phase_state 阶段对应的状态
    * @param[in] 错误码
    * @param[in] message 错误闲情
    */
    int reportStatus(EComPhase phase, EComState phase_state, EComErrorCode error_code = EComErrorCode::NO_ERR, const char* message = nullptr)
    {
        auto report_status = new (std::nothrow) RAIComStatus;
        if (nullptr == report_status)return -1;
        int32_t max_com_name_len = sizeof(report_status->com_name);
        int32_t max_message_len = sizeof(report_status->message);
        memset(report_status->com_name, 0, max_com_name_len);
        memset(report_status->message, 0, max_message_len);

        int32_t name_len = _name.size();
        int32_t copy_name_len = name_len >= max_com_name_len ? max_com_name_len - 1 : name_len;
        memcpy(report_status->com_name, _name.c_str(), copy_name_len);

        if (nullptr != message) {
            int32_t message_len = strlen(message);
            int32_t copy_message_len = message_len >= max_message_len ? max_message_len - 1 : message_len;
            memcpy(report_status->message, message, copy_message_len);
        }

        report_status->phase = phase;
        report_status->phase_state = phase_state;
        report_status->error_code = error_code;

        return output(RAICOM_OPORT_COM_STATUS, report_status);
    }

protected:
    ComOutputCallBack _out_cb = nullptr;
    void* _user_data = nullptr;
    ILogger* _logger = nullptr;
    std::string _name = "";
};

} } // end namespace radioai::core

// 生成 C ABI 工厂函数。每个组件 DLL 应只调用一次，ClassType 必须可默认构造。
#define RAICOM_EXPORT(ClassType)                                 \
radioai::core::ILogger* g_logger = nullptr;                      \
extern "C" RAICOM_API radioai::core::Com* createCom(const char* name, radioai::core::ILogger* logger) {          \
    g_logger = logger;                                           \
    auto obj = new (std::nothrow) ClassType();                   \
    if(nullptr == obj)return nullptr;                            \
    obj->setName(name);                                          \
    obj->setLogger(logger);                                      \
    return obj;                                                  \
}                                                                \
extern "C" RAICOM_API void destoryCom(radioai::core::Com* obj) { \
    delete obj;                                                  \
}                                                                



                                                           


