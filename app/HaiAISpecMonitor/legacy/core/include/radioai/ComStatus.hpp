#pragma once

#include "radioai/ComData.hpp"

/**
 * @brief 组件生命周期状态上报协议。
 *
 * RAICOM_OPORT_COM_STATUS 使用负端口号与普通业务输出区分。状态对象仍继承
 * ComData，因此沿用普通 Flow 的所有权和回调规则。
 */

namespace radioai { namespace core{

/// 保留给框架内部的状态输出端口，不与组件声明的正端口冲突。
constexpr int RAICOM_OPORT_COM_STATUS = -1;

/// 组件生命周期阶段；数值保持兼容历史状态机，不能随意重新编号。
enum class EComPhase : uint8_t
{
    INITIALIZE = 0,     // 初始化阶段
    START = 2,          // 启动阶段
    STOP = 4,           // 停止阶段
    UNINITIALIZE = 5    // 反初始化/资源释放阶段
};

/// 某个生命周期阶段的执行结果或当前进度。
enum class EComState : uint8_t
{
    UNKNOWN = 0,

    BEGIN = 1,          // 阶段开始
    PROCESSING = 2,     // 正在处理
    SUCCESS = 3,        // 执行成功
    FAILED = 4,         // 执行失败
    TIMEOUT = 5,        // 执行超时
};

/// 框架通用错误码；组件可在 message 中补充具体诊断信息。
enum class EComErrorCode : int32_t
{
    NO_ERR = 0,
    ERR_INVALID_PARAMETER = -1
};

/// 传递组件名称、阶段、状态和可选错误描述的状态包。
struct RAIComStatus : public radioai::core::ComData
{
    // 标识信息
    char com_name[128];            // 组件类型名称

    // 状态信息
    EComPhase phase;              // 当前生命周期阶段
    EComState phase_state;        // 当前阶段执行状态

    // 错误码
    EComErrorCode error_code;     // 错误码，0表示无错误

    // 详细描述
    char message[1024];           // 状态说明或错误描述
};

} } // end namespace radioai::core




                                                           


