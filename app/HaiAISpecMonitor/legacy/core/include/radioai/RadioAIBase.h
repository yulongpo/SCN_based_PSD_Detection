#pragma once

#include <stdio.h>
#include <stdint.h>
#include <cstdint>
#include <memory>

// 平台相关的 DLL/符号可见性控制。Windows 使用 __declspec，GCC/Clang 使用
// visibility 属性；其他平台保留为空宏以兼容静态构建。
#ifdef WIN32
#define HQEXPORT __declspec(dllexport) 
#define HQIMPORT __declspec(dllimport) 
#elif defined(__GNUC__) && __GNUC__ >= 4
#define HQEXPORT __attribute__ ((visibility ("default")))
#define HQIMPORT __attribute__ ((visibility ("default")))
#else
#define HQEXPORT
#define HQIMPORT
#endif

#if (defined WIN32)  
#pragma warning (disable: 4251)
#endif

// 核心库编译单元定义 RAICORE_EXPORTS，外部组件只看到导入声明。
#if (defined RAICORE_EXPORTS)  
#define RAICORE_API HQEXPORT
#else 
#define RAICORE_API HQIMPORT
#endif

// 组件 DLL 编译单元定义 RAICOM_EXPORTS，Flow 运行时通过这些符号创建组件。
#if (defined RAICOM_EXPORTS)  
#define RAICOM_API HQEXPORT   
#else 
#define RAICOM_API HQIMPORT
#endif

#ifdef __cplusplus

// 在 C++ 下关闭名称改编，保证 C ABI 的工厂函数可被动态库加载器查找。
#define EXTERN_C_BGN extern "C" {
// C语言代码结束
#define EXTERN_C_END }

#else

#define EXTERN_C_BGN 
#define EXTERN_C_END  

#endif

// 统一的命名空间宏，兼容历史代码中成对的 BGN/END 写法。
#define HQNAMESPACE_BGN(NAME) namespace NAME {

#define HQNAMESPACE_BGN2(NAME1, NAME2) namespace NAME1 { namespace NAME2 {

// 结束命令空间
#define HQNAMESPACE_END(NAME)  }

#define HQNAMESPACE_END2(NAME1, NAME2) } }
