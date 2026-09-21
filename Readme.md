# SCN-Based PSD Detection / ISA migration

这是 ISA 的 Qt 6.11 迁移版本。目标是保留原 ISA 的频谱监测界面和操作习惯，
同时将检测热路径从旧的 `Flow`、`Component`、`HQSigMF` 级联中移出。

当前版本提供：

- Qt Widgets 频谱监测界面和数据源控制区；
- BB60C、Harogic、File 三种数据源切换；
- 实时谱、最大保持谱、平均谱和检测标记显示开关；
- Direct2D 频谱图和瀑布图绘制后端，支持高 DPI 显示；
- 频谱图与瀑布图频率视窗同步；
- ISA 风格的频率输入和坐标轴交互。

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
SCN/PSD 检测算法。BB60C 已接入 Signal Hound BB API SDK，运行时会打开真实
设备并读取 dBm 扫频；Harogic 仍保留适配器边界和占位数据，状态栏会显示其
未接入状态，不把占位数据误报为硬件连接。

## 数据源与文件回放

### BB60C

BB60C 使用 Signal Hound BB API 连续扫频接口，支持中心频率、扫宽、RBW、
参考电平和 RBW 窗口设置。程序底部状态栏实时显示 BB60C 接入状态。

### Harogic

Harogic 当前未接入真实 HTRA SDK。界面和配置可以保存，但状态栏显示为未接入，
不将占位数据作为真实硬件数据使用。

### File

File 源默认用于离线验证。默认测试文件为：

```text
D:\project\isa\bin\data\spectrum_data_org\20260911_152444_651_Fc=2025000000_Bw=3950000000_Rbw=50000_Reflevel=-20.0_SpectrumLen=202242.dat
```

原 ISA `.dat` 文件由连续 little-endian `float32` 功率谱帧组成。文件名中的
`Fc`、`Bw`、`Rbw`、`Reflevel` 和 `SpectrumLen` 用于解析中心频率、扫宽、
RBW、参考电平和每帧点数。选择文件后，文件路径、起始频率、终止频率、RBW
和参考电平会自动回填；文件源下这些元数据控件只作展示，不能编辑。

同时保留 `.bin` float32 文件以及空白、逗号、分号、制表符分隔的文本文件。

## 频率设置与交互

频率控件内部统一使用 Hz，显示时自动选择合适单位。输入时支持完整单位和
ISA 风格缩写，单位大小写不敏感：

```text
2.4 GHz    2.4ghz    2400 MHz    2400M
2.4G       50 kHz    50KHZ       50K
50 Hz      50
```

无单位输入按 Hz 解释。中心频率/扫宽与起始频率/终止频率双向联动，频率范围
按 9 kHz 至 6 GHz 校验，BB60C 额外遵守设备的 6.4 GHz 物理上限。

频谱图交互如下：

- 在绘图区滚轮：按鼠标位置缩放频率范围；
- 在 X 轴下方滚轮：同样缩放频率范围，锚点跟随鼠标 X 位置；
- 在 X 轴下方按住左键拖动：水平平移频率范围；
- 在 Y 轴左侧滚轮：纵向缩放功率轴；
- 在 Y 轴左侧按住左键拖动：平移功率轴；
- 在绘图区左键框选：缩放到选中的频率范围；
- 在绘图区中键拖动：水平平移；
- 右键还原或双击：恢复全频段和默认功率范围。

瀑布图跟随频谱图频率视窗同步更新，保留最新帧在顶部、100 行历史记录、
ISA 风格色阶和绘图区网格。

## 绘制与显示

频谱图和瀑布图使用 Direct2D 绘制，并按 Qt 逻辑坐标与 Windows DPI 显式换算，
适配 100%、125%、150% 和 200% 等高 DPI 缩放环境。

频谱图保留完整原始频谱数据，后台按当前可见范围和像素宽度生成显示包络，
避免在 UI 线程构建超大路径。瀑布图使用原始频谱历史缓冲和最新请求覆盖策略，
缩放、拖动和窗口变化时先显示快速预览，再生成高质量图像。

监测开始新一轮采集时，频谱图、瀑布图、最大谱、平均谱和信号统计都会清空，
首帧到达后重新开始显示。最大谱和平均谱只保留最近 100 帧数据。

频谱缩放范围和选中频点不持久化，程序启动时默认恢复当前数据源的全频段视图。

## 配置持久化

程序使用 `QSettings` 保存窗口几何、分割器布局、数据源类型、文件路径、中心
频率、扫宽、起止频率、RBW、参考电平、RBW 窗口、文件循环和帧率等参数。

频谱视窗范围和选中频点不保存。程序不会因为恢复配置而自动启动硬件采集。

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
.\out\build\vs2026-qt611-debug\app\HaiAISpecMonitor\Debug\HaiAISpecMonitord.exe
```

BB60C 构建默认使用 `D:/project/isa2/3rdparty/bb_series` 中的
`bb_api.h`、`bb_api.lib`、`bb_api.dll` 和 `ftd2xx.dll`。如果 SDK 安装在
其他位置，配置 CMake 时覆盖 `SCN_BB60C_SDK_ROOT`。程序启动后，状态栏会
分别显示 BB60C 与海得罗捷的接入状态；当前海得罗捷适配器仍是占位实现，
因此显示为“未接入”。

## 检查与测试

执行构建和静态检查：

```powershell
cmake --build --preset vs2026-qt611-debug --target HaiAISpecMonitor
ctest --preset vs2026-qt611-debug
git diff --check
```

当前仓库没有注册 CTest 用例，`ctest` 会报告 `No tests were found!!!`。
实际的文件回放、频谱/瀑布图交互以及 BB60C 硬件验收由用户在 VS 2026 中完成。

## 运行时状态

程序底部状态栏显示 CPU、GPU、RAM、当前频率范围、RBW、采集时间、当前数据源、
BB60C 接入状态、海得罗捷接入状态、设备状态、网络状态和自检结果。

算法检测接口、三种数据源接口和 `SpectrumFrame`/`DisplaySnapshot` 数据契约
保持分层兼容；检测算法本身不在本迁移版本中实现。
