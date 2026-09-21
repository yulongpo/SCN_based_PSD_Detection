#pragma once

#include "radioai/RadioAIBase.h"

namespace radioai { namespace core{

/**
 * @brief RadioAI 组件间传输对象的最小公共接口。
 *
 * ComData 只规定多态析构和可选的深复制入口。Flow 以基类指针传递数据，
 * 具体 ICD 结构负责实现 clone()；未实现 clone() 的类型不能安全地跨异步边界复制。
 */
class ComData
{
public:
    virtual ~ComData() = default;

    /**
    * @brief 组件数据克隆
    * @return 返回克隆对象
    */
    /// 返回由调用方负责释放的独立对象；基类默认返回 nullptr 表示不支持克隆。
    virtual ComData* clone() { return nullptr; }
};

} } // end namespace radioai::core

                                                           


