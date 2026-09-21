#pragma once

#include "radioai/RadioAIBase.h"
#include <cstdarg>

namespace radioai { namespace core{

/**
 * @brief 核心库使用的轻量级日志门面。
 *
 * log() 负责阈值过滤和固定长度格式化，具体输出目标由派生类实现 logImpl()。
 * 格式化缓冲区固定为 1024 字节，超长消息会被截断。
 */
class ILogger
{
public:
    /**
     * @brief：日志等级
     */
    enum class Level : int32_t
    {
        kDEBUG = 1,           //只在开发/调试阶段关心的细粒度信息
        kINFO = 2,            //关键业务/系统正常流程节点，用来跟踪系统健康状态。
        kWARNING = 3,         //发生了异常但能自动恢复或潜在风险，尚未影响功能。
        kERROR = 4,           //运行时错误，某个操作失败，但系统仍能继续服务其它请求。
        kFATAL = 5,           //致命错误，严重到程序无法继续运行，即将退出或崩溃。
    };

    /**
     * @brief：日志记录接口
     * @param level 日志等级
     * @param fmt 格式化字符串
     * @param args 参数
     */
    /// 仅当 level 不低于当前阈值时格式化消息并转发给后端。
    void log(Level level, const char* fmt, ...) {
        if (level < _level_thresh)return;
        char buf[1024] = { 0 };
        va_list args;
        va_start(args, fmt);
        std::vsnprintf(buf, 1023, fmt, args);
        va_end(args);
        logImpl(level, buf);
    }

    /// 设置最低输出等级；例如 kWARNING 会过滤 DEBUG/INFO。
    void setLevelThresh(radioai::core::ILogger::Level level_thresh) { _level_thresh = level_thresh; }

    ILogger() = default;
    virtual ~ILogger() = default;

protected:
    /**
     * @brief：日志实现接口
     * @param level 等级
     * @param msg
     */
    /// 后端输出钩子；实现必须保证异常不穿过日志调用边界。
    virtual void logImpl(Level level, const char* msg) noexcept = 0;

private:
    radioai::core::ILogger::Level _level_thresh = radioai::core::ILogger::Level::kDEBUG;
};

}}
