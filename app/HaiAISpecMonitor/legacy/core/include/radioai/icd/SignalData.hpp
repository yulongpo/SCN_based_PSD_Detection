#pragma once

#include "radioai/ComData.hpp"

/**
 * @file SignalData.hpp
 * @brief RadioAI 业务层信号分析、频谱文件和告警规则 ICD。
 *
 * 这些结构通过 ComData 在组件之间传输。带指针的列表结构只描述外部内存，
 * 不统一承担深拷贝和释放责任；生产代码必须在接口文档中约定指针所有者。
 * 频率统一使用 Hz，绝对时间使用 ns，持续时间字段按成员注释使用 ms。
 */

/// 请求在指定时频范围内执行信号分析。
struct SigAnaReq : public radioai::core::ComData {
    int32_t task_id;              //任务ID
    int64_t bgn_freq;             //起始频率，单位Hz
    int64_t end_freq;             //终止频率，单位Hz
    int64_t bgn_time;             //起始时间，单位ns
    int64_t end_time;             //终止时间，单位ns
};

/// 返回分析任务状态、进度和结果目录。
struct SigAnaResp : public radioai::core::ComData {
    int32_t task_id;            //任务ID
    int32_t status;             //状态 {-1：异常，0：完成，1：运行中}
    float progress;             //进度，范围[0,1]
    char result_dir[1024];       //结构存储目录
};


/// 描述采集/扫描信号源的中心频率、扫宽、RBW 和参考电平。
struct SourceDetail : public radioai::core::ComData {
    char name[128];        //信号源名称
    int64_t fc;            //中心频率，单位Hz
    int64_t span;          //扫宽，单位Hz
    int32_t rbw;           //带宽分辨率，带宽Hz
    int32_t ref_level;     //参考电平，单位dBm
    int64_t timestamp;     //时间戳
};

/// 一条告警规则；enable、载波类型和告警等级使用协议约定的整数枚举值。
struct SignalAlarmRule : public radioai::core::ComData {
    int64_t rule_id;       //规则ID
    char name[256];        //规则名称
    int32_t enable;        //是否启动，0 - 未启动，1 - 启动
    int64_t bgn_freq;      //起始频率，单位Hz。
    int64_t end_freq;      //终止频率，单位Hz。
    int32_t carry_type;    //载波类型 0 - 常在，1 - 突发
    int32_t alarm_level;   //告警等级 0 - 正常，1 - 一般， 2 - 严重
    int32_t sig_min_bw;    //信号最小带宽，单位Hz。
    int32_t sig_max_bw;    //信号最大带宽，单位Hz。
    char note[1024];       //备注
};

/// 告警规则数组视图，item 的分配和释放由生产者/消费者协议决定。
struct SignalAlarmRules : public radioai::core::ComData {
    int32_t num;           //规则个数
    SignalAlarmRule* item; //规则列表
};

/// 对告警规则执行新增、删除或更新操作的请求。
struct SignalAlarmRuleOpReq : public radioai::core::ComData {
    int32_t op_type;       //操作状态 0 - 新增 1 - 删除 2 - 更新 
    SignalAlarmRule rule;  //待操作的规则
};

/// 告警规则操作的成功/失败回复。
struct SignalAlarmRuleOpResp : public radioai::core::ComData {
    int32_t success;            //0 - 成功，其它失败
};

/// 已保存频谱文件的索引元数据和统计信息。
struct SpectrumFileInfo : public radioai::core::ComData {
    int64_t file_id;       //文件ID
    char file_name[1024];  //文件名
    char source_name[128]; //数据源设备名称
    int64_t fc;            //中心频率，单位Hz
    int64_t span;          //扫宽，单位Hz
    int32_t rbw;           //带宽分辨率，带宽Hz
    int32_t ref_level;     //参考电平，单位dBm
    int64_t bgn_time;      //起始时间
    int64_t end_time;      //终止时间
    int32_t size;          //文件大小，单位MHz
    int32_t signal_num;    //信号个数
    int32_t alarm_num;     //告警个数
    int32_t len_per_spec;  //单帧频谱长度
    int32_t spec_num;      //频谱帧数
};

/// 频谱文件数组视图。
struct SpectrumFileInfos : public radioai::core::ComData {
    int32_t num;               //文件个数
    SpectrumFileInfo* items;   //列表
};

/// 按文件 ID 查询文件内信号明细。
struct SignalDetailPerFileQueryReq : public radioai::core::ComData {
    int64_t file_id;       //文件ID
};

/// 返回文件内信号的频率、告警属性、出现次数和时间列表。
struct SignalDetailPerFileQueryResp : public radioai::core::ComData {
    struct TimeRange {
        int64_t bgn_time;
        int64_t end_time;
    };

    struct SignalItem{
        int64_t id;           //信号ID
        int64_t fc;           //信号中心频率
        int32_t bw;           //信号带宽
        int8_t carry_type;                       //载波类型 0 - 常在 1 - 突发
        int8_t alarm_level;                      //告警等级 0-正常、1-一般、2-严重
        int64_t recent_time;  //最近出现时间
        int32_t burst_num;    //突发次数
        int64_t avg_duration; //平均时长，单位ms

        TimeRange* times;     //出现的时间列表
    };

    int64_t file_id;       //文件ID
    int32_t signal_num;    //信号个数
    SignalItem* item;      //信号列表
};

/// 批量删除频谱文件的请求。
struct SpectrumFileDelReq : public radioai::core::ComData {
    int32_t file_num;      //文件个数 
    int64_t* file_ids;     //待删除的文件列表
};

struct SpectrumFileDelResp : public radioai::core::ComData {
    int32_t success;            //0 - 成功，其它失败
};

/// 删除指定文件中的一组信号记录。
struct SignalDetailPerFileDelReq : public radioai::core::ComData {
    int64_t file_id;         // 文件ID
    int32_t signal_num;      // 信号个数
    int64_t* signal_ids;     // 待删除的信号ID列表
};

//信号删除回复描述结构
struct SignalDetailPerFileDelResp : public radioai::core::ComData {
    int32_t success;            //0 - 成功，其它失败
};

/// 存储队列或磁盘发生溢出时的通知事件；当前结构仅保留协议占位字段。
struct RecordOverflowEvent : public radioai::core::ComData {
    int8_t empty;           //占位符
};
