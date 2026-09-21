#pragma once
#include <functional>
#include "radioai/ComFlow.hpp"
#include "radioai/Logger.hpp"
#include "radioai/icd/HQSigMF.hpp"
#include "radioai/icd/SignalData.hpp"

using ParamTable = radioai::core::IComFlow::ParamTable;

namespace haiq
{
    enum class CMDType : int
    {
    	CMD_INIT,			 	   // 初始化
        CMD_START,      	 	   // 启动
        CMD_PAUSE,      	 	   // 暂停
        CMD_RESUME,     	 	   // 恢复
        CMD_STOP,       	 	   // 停止
        CMD_RESET,      	 	   // 重置
        CMD_PARAM,      	 	   // 参数下发
        CMD_RULE_OPER_REQ,   	   // 告警规则操作请求
        CMD_FILE_SIG_SEARCH_REQ,   // 文件信号明细查询请求
        CMD_FREQ_FILE_DEL_REQ,	   // 频谱文件删除请求
        CMD_SIGNAL_DEL_REQ,		   // 信号删除请求
        CMD_REFRESH_FILE_LIST,	   // 刷新频谱文件列表
    	CMD_WHITE_OPER_REQ,   	   // 白名单操作请求
    };

    enum class Result : int
    {
        Success,        // 成功
        Fail            // 失败
    };

    // 请求来源标识：下发时在 GuiRequestData._reqSrc 设置，响应时随请求回传以区分响应归属
    enum ReqSource : int
    {
        ReqSource_Default = 0,    // 默认（采集监测）
        ReqSource_Storage = 100,  // 存储策略参数下发
    };

    struct GuiRequestData
    {
        haiq::CMDType _cmdType;                         	// 命令
        ParamTable _paramTable;                         	// 参数---CMD_PARAM
    	int _uuid;											// 自定义标识
    	int _reqSrc = ReqSource_Default;					// 请求来源标识（见 ReqSource）
    	std::shared_ptr<radioai::core::ComData> _comData;	// 操作结构
    };

    struct GuiResponseData
    {
        haiq::CMDType _cmdType;                         // 命令
        Result _result;                                 // 响应状态
    	int _uuid;										// 自定义标识
    	int _reqSrc;									// 请求来源标识（见 ReqSource）
    	SignalAlarmRuleOpResp _ruleOpResp;				// 告警规则操作响应
    };

    using RequsetCallback = std::function<bool(GuiRequestData&)>;       // 添加请求数据
    using ResponseCallback = std::function<void(GuiResponseData&)>;     // 添加请求数据
}


//消息定义
enum EMsgId
{
	MSG_TF_DATA = 0x0f000001,			             //时频数据
	MSG_MAX_SPECTRUM_DATA = 0x0f000002,              //最大谱数据
	MSG_AVG_SPECTRUM_DATA = 0x0f000003,              //平均数据
	MSG_CARRY_DATA = 0x0f000004,			         //载波数据
	MSG_SPECTRUM_AND_MARK_DATA = 0x0f000005			 //频谱加标记数据
};

/*频谱数据 -- MSG_SPECTRUM_DATA*/
struct SSpectrumData {
	int64_t time{ 0 };        //时间 ns
	int64_t span{ 0 };        //时长 ns
	int64_t bgn_freq{ 0 };	  //起始频率（Hz）
	int64_t end_freq{ 0 };	  //终止频率（Hz）
	int32_t fft_len{ 0 };     //fft长度
	int32_t frame_num{ 0 };   //帧数
	float* data{ nullptr };   //数据列表	
};

/*信号对象*/
struct SSigObject {
	int32_t id;                     //信号ID

	//参数
	int64_t bgn_time;               //起始时间 ns
	int64_t end_time;               //终止时间 ns
	int64_t bgn_freq;               //起始频率 hz
	int64_t end_freq;               //终止频率 hz
	int8_t emitter_class[64] = "UNKNOWN";//辐射源个体
	int8_t modulation[64] = "UNKNOWN";//调制
	int8_t signal_class[64] = "UNKNOWN";//信号种类
	float signal_level;             //信号电平
	float noise_level;              //噪声电平

	//类别
	std::string protocol;           //协议
	float protocol_confidence;      //协议置信度

	//数据
	int64_t fs;                     //采样率
	int32_t size;                   //字节大小
	float* data;                    //IQ窄带数据
};

/*信号对象列表*/
struct SSigObjects {
	int32_t num;
	SSigObject* sig_objects;
};

//频谱加标记数据
struct SSpectrumAndSignalData {
	SSpectrumData* spectrum_data;
	SSigObjects* sig_objects;
};

#ifndef FAIL
#define FAIL (-1)
#endif

#ifndef OK
#define OK (0)
#endif

#ifndef RETURN_IF
#define RETURN_IF(exp, value) if(exp){return value;}
#define RETURN_VOID_IF(exp) if(exp){return;}
#endif

//日志宏定义
extern radioai::core::ILogger* g_logger;
#define LOG_DEBUG(fmt, ...) if(nullptr != g_logger)g_logger->log(radioai::core::ILogger::Level::kDEBUG, "[GUI] " fmt, ##__VA_ARGS__);
#define LOG_INFO(fmt, ...) if(nullptr != g_logger)g_logger->log(radioai::core::ILogger::Level::kINFO, "[GUI] " fmt, ##__VA_ARGS__);
#define LOG_WARN(fmt, ...) if(nullptr != g_logger)g_logger->log(radioai::core::ILogger::Level::kWARNING, "[GUI] " fmt, ##__VA_ARGS__);
#define LOG_ERROR(fmt, ...) if(nullptr != g_logger)g_logger->log(radioai::core::ILogger::Level::kERROR, "[GUI] " fmt, ##__VA_ARGS__);
#define LOG_FATAL(fmt, ...) if(nullptr != g_logger)g_logger->log(radioai::core::ILogger::Level::kFATAL, "[GUI] " fmt, ##__VA_ARGS__);
