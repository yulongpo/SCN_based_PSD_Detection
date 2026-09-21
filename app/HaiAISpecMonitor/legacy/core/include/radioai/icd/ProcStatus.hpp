#pragma once
#include "radioai/ComData.hpp"

/**
 * @file ProcStatus.hpp
 * @brief 采集/处理任务的进度状态对象。
 *
 * current_packet 与 total_packet 用于计算进度；output_file 和 part_index 主要
 * 供分片文件采集源使用。该结构由 ComData 多态传输，默认状态为 PROCESSING。
 */

/// 处理任务的运行态和终态；数值是跨组件协议的一部分。
enum class ProcStatusEnum : int32_t
{
    COMPLETED  = 0,   // 处理完成
    PROCESSING = 1,   // 处理中
};

/// 可随数据流上报的任务进度对象。
struct ProcStatus : public radioai::core::ComData
{
    int64_t current_packet = 0;   // 当前已发送包数
    int64_t total_packet   = 0;   // 总包数
    int32_t status = static_cast<int32_t>(ProcStatusEnum::PROCESSING);
    char    output_file[512] = {0};  // 当前写入的文件路径（采集源用）
    int32_t part_index = 0;          // 当前分片序号（采集源用）
};
