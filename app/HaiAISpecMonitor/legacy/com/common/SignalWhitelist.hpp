#pragma once

#include "radioai/ComData.hpp"

//信号白名单结构定义---------------------------------------

//信号白名单结构单元
struct SignalWhitelist : public radioai::core::ComData {
    int64_t id;       //规则ID
    char name[256];        //规则名称
    int32_t enable;        //是否启动，0 - 未启动，1 - 启动
    int64_t bgn_freq;      //起始频率，单位Hz。
    int64_t end_freq;      //终止频率，单位Hz。
    char note[1024];       //备注
};

//信号白名单结构列表
struct SignalWhitelists : public radioai::core::ComData {
    int32_t num;           //规则个数
    SignalWhitelist* item; //规则列表
};

//信号白名单操作请求描述结构
struct SignalWhitelistOpReq : public radioai::core::ComData {
    int32_t op_type;       //操作状态 0 - 新增 1 - 删除 2 - 更新 
    SignalWhitelist item;  //待操作的项
};

//信号白名单操作操作回复描述结构
struct SignalWhitelistOpResp : public radioai::core::ComData {
    int32_t success;            //0 - 成功，其它失败
};