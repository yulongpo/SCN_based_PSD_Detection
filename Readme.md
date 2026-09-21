# SCN-Based PSD Detection / ISA migration

这是 ISA 的 Qt 6.11 迁移骨架。当前目标是保留频谱监测界面和操作习惯，
同时将检测热路径从旧的 Flow、Component、HQSigMF 级联中移出。

## 当前架构

```text
app/HaiAISpecMonitor/       Qt 6 UI、controller、viewmodel、频谱图、瀑布图
application/                MonitoringSession、SourceManager、PresentationModel
source/                     BB60C、Harogic、File 三种数据源
algorithm/                  DetectionEngine 和强类型数据接口
runtime/                    SpscQueue、BufferPool、PerformanceMonitor
tools/DetectionLab/         与产品共用接口的调试入口
tests/                      由用户补充的测试边界
app/HaiAISpecMonitor/legacy/ 原 ISA UI、资源和依赖快照，仅供迁移参考
```

`algorithm/DetectionEngine` 目前只实现生命周期、配置和结果接口，不包含
SCN/PSD 检测算法。BB60C 与 Harogic 适配器提供确定性的频谱占位数据，
因此无需厂商 SDK 即可验证 UI、线程和快照通路；厂商 API 的接入位置已经
固定在对应 source adapter 中。

## Qt 6.11 / VS 2026 编译

已提供 `CMakePresets.json`，使用本机 Qt：

```text
D:/softwares/Qt/6.11.1/msvc2022_64
```

在 VS 2026 中：

1. 打开 `D:\project\SCN_based_PSD_Detection` 文件夹。
2. 选择 `vs2026-qt611-debug` 配置并等待 CMake 配置完成。
3. 构建 `HaiAISpecMonitor` 或解决方案全部目标。
4. 需要发布版本时选择 `vs2026-qt611-release`。

命令行等价方式：

```powershell
cmake --preset vs2026-qt611-debug
cmake --build --preset vs2026-qt611-debug --target HaiAISpecMonitor

# 可选调试工具
cmake --build --preset vs2026-qt611-debug --target DetectionLab
```

运行主程序：

```powershell
.\out\build\vs2026-qt611-debug\Debug\HaiAISpecMonitor.exe
```

File replay 遵循原 ISA 的离线频谱格式：`.dat` 文件由连续 little-endian
`float32` 功率谱帧组成，文件名中的
`Fc=..._Bw=..._Rbw=..._Reflevel=..._SpectrumLen=...` 用于确定频率范围、
RBW、参考电平和每帧点数。选择文件后 UI 会自动回填这些参数，并支持到
文件尾停止或循环回放。同时保留 `.bin` float32 和空白/逗号/分号/制表符
分隔文本文件。当前测试目标由用户在 `tests/` 下补充；本轮没有执行测试。
