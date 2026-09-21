#pragma once

#include "radioai/ComData.hpp"

namespace radioai { namespace core{

/**
 * @brief 组件流结果观察者接口。
 *
 * 观察者接收 Flow 输出端口的数据，不取得 ComData 的长期所有权；若需要异步
 * 保存，应在回调内完成克隆或建立明确的 shared_ptr 生命周期。
 */
class IComFlowObserver {
public:
    /// 多态基类析构，允许运行时通过接口指针销毁观察者。
	virtual ~IComFlowObserver() = default;

    /**
     * @brief 结果上报
     * @param cf_name 组件流名称
     * @param cf_port_id 组件流端口序号
     * @param data 端口数据
     * @reutrn 0 - 成功，其它失败
     */
    /// 接收某个 Flow 端口的结果；返回非零表示观察者处理失败。
	virtual int onResult(const char* cf_name, int cf_port_id, ComData* data) = 0;
};

}}
