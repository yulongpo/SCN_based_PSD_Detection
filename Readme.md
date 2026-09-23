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
- ISA TensorRT SCN 大带宽检测、16 帧平均/最大谱融合及轻量跨帧跟踪；
- 独立采集/检测线程、有界通道和可复现的 DetectionLab 离线诊断工具。

## 当前架构

```text
app/HaiAISpecMonitor/       Qt 6 UI、controller、viewmodel、频谱图、瀑布图
application/                MonitoringSession、SourceManager、PresentationModel
source/                     BB60C、Harogic、File 三种数据源
algorithm/                  预处理、累积、TensorRT SCN、CNR、融合、跟踪、诊断
runtime/                    BoundedChannel、SpscQueue、BufferPool、PerformanceMonitor
tools/DetectionLab/         与产品共用 DetectionEngine 的回放、导出和对照工具
tests/                      CPU 算法、通道、异步轮次、文件回放和哈希测试
app/HaiAISpecMonitor/legacy/ 原 ISA UI、资源和依赖快照，仅供迁移参考
```

`algorithm/DetectionEngine` 已实现 SCN 检测链路，生产推理仅使用 ISA 的 TensorRT
engine，不引入 ONNX Runtime 或 Python。白名单、告警规则及信号分类不属于该算法。
BB60C 已接入 Signal Hound BB API SDK，运行时会打开真实
设备并读取 dBm 扫频；Harogic 仍保留适配器边界和占位数据，状态栏会显示其
未接入状态，不把占位数据误报为硬件连接。

## 数据源与文件回放

### BB60C

BB60C 使用 Signal Hound BB API 连续扫频接口，支持中心频率、扫宽、RBW、
参考电平和 RBW 窗口设置。程序底部状态栏实时显示 BB60C 接入状态。

接入 BB60C 或已连接的 Harogic 实时源后，即使正在监测或暂停查看，仍可编辑中心／起止频率、
扫宽、RBW、参考电平和 RBW 窗口；按 Enter 或离开输入框后会重新配置数据源，并恢复原运行／暂停状态。
重新配置会开启新的检测轮次，旧频率网格的检测累计不会与新参数混用。监测期间数据源选择仍锁定，
FILE 源参数继续按文件元数据只读显示。

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

### 实时源录制与回放

BB60C 和海得罗捷实时源可以在“系统设置 → 存储”页的“实时源录制”区域启用录制。
录制在首个有效实时频谱帧到达后开始，保存目录默认为程序目录下的
`data_record`，也可以在设置页中选择其他目录。文件格式与
当前 FILE 源默认文件一致：连续 little-endian `float32` PSD 帧，扩展名为 `.dat`。
文件名自动写入 `Fc`、`Bw`、`Rbw`、`Reflevel` 和 `SpectrumLen`，因此录制完成后
可以直接在“录制回放”页打开，或切换到 FILE 源选择该文件回放。录制过程遇到频率
网格变化会停止当前文件并显示错误，避免生成无法连续读取的文件。

录制文件加入回放列表后，打开“详情”可以查看当前回放得到的业务信号结果，并使用
“导出信号列表”导出 CSV 或 JSON。导出内容包含业务 ID、中心频率、带宽、信号类型、
告警等级、最近出现时间和出现次数；白名单替换结果的归并来源等详情可在表格提示中查看。
录制开关和保存目录保存在 `QSettings` 的 `recording/enabled` 与
`recording/directory`，不会自动启动采集。

文件检测队列满时暂停读入，不丢检测帧；文件循环在末尾检测完成后重置算法。
DAT 不包含逐帧采集时间，跟踪使用“文件帧索引 / 配置帧率”的逻辑回放时间，
不以推理耗时推算信号时间。实时设备队列满时丢弃最旧待检测帧，统计丢帧数。

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

- 频谱图右上角的实时谱、最大谱、平均谱和检测标记开关仅在鼠标移入频谱图时显示，离开后自动隐藏；
- 在绘图区滚轮：按鼠标位置缩放频率范围；
- 在 X 轴下方滚轮：同样缩放频率范围，锚点跟随鼠标 X 位置；
- 在 X 轴下方按住左键拖动：水平平移频率范围；
- 在 Y 轴左侧滚轮：纵向缩放功率轴；
- 在 Y 轴左侧按住左键拖动：平移功率轴；
- 在绘图区左键框选：缩放到选中的频率范围；
- 在绘图区中键拖动：水平平移；
- 右键还原或双击：恢复全频段和默认功率范围。

瀑布图与频谱图之间的频率导航条表示完整采集频段和当前视窗：拖动选区中部
可以平移，拖动左右手柄可以分别调整起止频率，滚轮按锚点缩放。导航条只改变显示
轨道和选区，不显示额外刻度文字；它不改变采集、检测、白名单和告警规则的频率
范围，导航视窗也不会写入持久化配置。

瀑布图跟随频谱图频率视窗同步更新，保留最新帧在顶部、100 行历史记录和
ISA 风格色阶；频率范围与刻度统一由上方导航栏提供，瀑布图本身不绘制网格。
瀑布图降采样采用 P75 稳健底噪统计，只有高出 P75 至少 6 dB 的孤立尖峰才保留
尖峰值，避免随机噪声峰抬高整列底噪。

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

系统设置→显示中的“显示刷新”控件可设置界面与检测结果发布频率，范围为 `1~120 fps`，默认
`30 fps`，并保存到 `ui/displayRefreshRateHz`。该设置只控制最新数据提交到界面的
节拍，不改变 BB60C、Harogic 或 File 源的采集／回放速率；采集频率仍由数据源参数控制。
频谱、瀑布、检测标记、白名单业务结果、信号表和统计卡片统一在该显示节拍提交，
中间到达的结果只保留最新版本，避免异步回调造成闪烁。

系统设置→显示中的“显示动态范围”设置频谱图纵轴和瀑布图色阶的共同范围，取值为 `20~160 dB`、
步进 `5 dB`，默认 `80 dB`，保存到 `ui/displayDynamicRangeDb`。两图上限为当前参考电平，
下限为“参考电平−动态范围”；修改后立即重绘，瀑布历史和频谱最大／平均保持状态不清空。

“系统设置 → SCN 检测”保存独立的 `detection/*` 参数。算法累积默认 16 帧，
与 UI 最近 100 帧最大谱/平均谱独立。切换显示开关不会改变检测结果。
更换模型、GPU 或切换检测开关需先停止监测；阈值更新在下一处理周期生效。
SCN 草稿仅由“应用 SCN 设置”提交，开始采集使用已接受的检测配置。

## SCN 检测

```text
原始 PSD → 最近16帧平均谱/最大谱 → 32768点切窗（步长16384）
         → min/max归一化 → ISA TensorRT SCN → 解码/NMS/CNR
         → 跨窗融合/双分支融合 → 约束关联/边界稳定/稳定频段重测 → 白名单与告警/UI结果
```

从首帧开始渐进检测，满 16 帧后保持滑动窗口。默认置信度 0.4、NMS IoU 0.5、
CNR 3 dB；窗口尾部按最低值补齐，仅有效原始区域参与电平计算。
频率定位使用 `startFrequencyHz + binIndex * binWidthHz`，不对整谱插值或抽样。

信号表与检测标记显示应用层整理后的业务结果，信号类型为“未分类”。白名单和告警规则在
应用层独立执行：命中白名单的原始结果会被移除，并由对应白名单配置频段生成 `W-<ID>`
业务结果；白名单仍不会抑制告警。模型不可用时显示具体错误，原始绘图仍可工作。
完整参数、数据语义、部署和验收见 [SCN 开发与验收说明](docs/scn_detection.md)。

跟踪使用频段重叠、带宽比和中心距离共同约束的一对一关联。默认用最近 5 次已接受边界的中位数
与 `α=0.35` EMA 抑制抖动；大幅突变需唯一关联并连续确认 3 个不同检测帧。稳定频段在完整累积谱上
重新测量电平和 CNR，白名单、告警、信号表、标记和回放结果使用稳定结果；原始融合结果仍保留用于详情
和对照。可在“系统设置 → SCN 检测 → 跟踪与边界稳定”调整稳定开关、门限、历史窗口、平滑系数和确认次数。

## 白名单、告警规则与历史

系统设置中提供“白名单”和“告警规则”页面。频段采用相交匹配，端点相接也算
命中：

```text
signalEnd >= policyStart && signalStart <= policyEnd
```

白名单支持名称、启用状态、频段和备注。命中一条白名单的多个原始结果合并为一条
固定白名单频段结果；同一原始结果命中多条白名单时分别生成结果，允许频段重叠。
替换结果的电平、CNR 和置信度来自命中结果中信号电平最高的代表结果，原始 ID 保留在详情和历史中。
告警规则支持频段、带宽上下限、可选的
最小信号电平/CNR/置信度、一般/严重等级、连续命中次数、最短持续时间和解除延时。
同一信号的多条规则独立判断，活动事件取最高等级；白名单命中不会免除告警。
旧 ISA 的 `whitelists.json` 和 `alarm_rules.json` 可从设置页导入，`carry_type`
只作为兼容字段读取并在实际匹配中忽略。

配置保存为应用目录下的 `config/policy.json`，使用 `QSaveFile` 原子替换；告警事件
保存到 `config/policy.sqlite`。设置页的“告警历史”支持查询、确认选中事件以及
CSV/JSON 导出。历史事件按监测轮次、分段、业务来源和业务 ID 建立，不会因 UI 刷新重复计数；
白名单替换事件同时保存代表原始 ID 和全部归并原始 ID。
程序暂停、检测失败或截断结果不会被当作信号消失；文件源事件使用文件逻辑时间。

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

# 编译 CPU、策略和 SQLite 存储测试
cmake --build --preset vs2026-qt611-debug --target scn_tests scn_session_tests scn_policy_tests scn_policy_storage_tests
```

运行主程序：

```powershell
.\out\build\vs2026-qt611-debug\app\HaiAISpecMonitor\Debug\HaiAISpecMonitord.exe
```

BB60C 构建默认使用项目内 `third_party/bb60c` 中的
`bb_api.h`、`bb_api.lib`、`bb_api.dll` 和 `ftd2xx.dll`。如果 SDK 安装在
其他位置，配置 CMake 时覆盖 `SCN_BB60C_SDK_ROOT`。程序启动后，状态栏会
分别显示 BB60C 与海得罗捷的接入状态。Harogic 默认使用项目内
`third_party/harogic` 中的 HTRA SDK；配置成功后执行真实 SWP 扫频并显示硬件
dBm 频谱，SDK 或运行库缺失时明确显示为“未接入”，不会回退到合成数据。

| CMake 参数 | 本机默认值 |
| --- | --- |
| `SCN_HAROGIC_SDK_ROOT` | `${sourceDir}/third_party/harogic` |

默认同时启用 TensorRT 后端：

| CMake 参数 | 本机默认值 |
| --- | --- |
| `SCN_ENABLE_TENSORRT` | `ON` |
| `SCN_TENSORRT_ROOT` | `${sourceDir}/third_party/tensorrt` |
| `SCN_MODEL_SOURCE` | `${sourceDir}/models/scn_model.engine` |
| `CUDAToolkit_ROOT` | `C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.4` |

使用 TensorRT 10.11.0，C++ Runtime API 不编译 CUDA 核函数。部署程序会复制模型和
TensorRT SDK 自带的配套运行库（含 CUDA Runtime 12.9），不要用 Toolkit 12.4 的
同名 `cudart64_12.dll` 覆盖。TensorRT headers、导入库、运行时 DLL 和默认 engine
已复制到当前项目的 `third_party/tensorrt` 与 `models`；这些二进制资产仍由
`.gitignore` 排除，避免将大体积及受许可约束的厂商二进制提交到源码仓库。
`SCN_ENABLE_TENSORRT=OFF` 可构建明确报告检测不可用的版本，不会切换为假检测。

## 依赖包部署

BB60C、Harogic HTRA、TensorRT 的完整导入库和运行库，以及默认
`models/scn_model.engine`，作为 GitHub Release 的依赖 ZIP 发布。部署到新机器时，
先克隆 `scn_dev` 分支，再从仓库的 Releases 页面下载对应版本的依赖包，并在仓库
根目录解压，确保生成 `third_party/bb60c`、`third_party/harogic`、
`third_party/tensorrt` 和 `models/scn_model.engine`。Qt 6.11.1 与 CUDA Toolkit
不包含在该 ZIP 中，需要在目标机器单独安装。

依赖目录和模型由现有 `.gitignore` 排除；源码仓库提交头文件和构建配置，Release
资产提供大体积 DLL、LIB 及 engine 文件。

## Release 安装包

使用 VS 2026 和 Qt 6.11.1 构建发布版并生成 Windows x64 自解压安装包：

```powershell
.\tools\package_release.ps1
```

安装包输出到 `out/packages/HaiAISpecMonitor-0.1.0-win-x64-Setup.exe`。安装包内含
Qt 发布运行库、BB60C 和 Harogic 运行库、TensorRT/CUDA 运行库以及默认 SCN 模型；
不包含 Qt/CUDA 开发环境，也不包含本机的 `config/` 配置和告警历史。运行安装包后，
默认安装到 `%ProgramFiles%\SCN\HaiAISpecMonitor` 并启动程序。

仅重新打包已有 Release 构建产物时使用：

```powershell
.\tools\package_release.ps1 -SkipBuild
```

该脚本依赖目标机器已安装 7-Zip（默认路径为 `C:\Program Files\7-Zip`）。

DetectionLab 快速入口：

```powershell
$lab = '.\out\build\vs2026-qt611-debug\tools\DetectionLab\Debug\DetectionLab.exe'
& $lab --inspect-model
& $lab --file 'D:\project\isa\bin\data\spectrum_data_org\20260911_152444_651_Fc=2025000000_Bw=3950000000_Rbw=50000_Reflevel=-20.0_SpectrumLen=202242.dat' --frames 32 --output '.\out\scn-results.jsonl'
```

## 检查与测试

执行构建和静态检查：

```powershell
cmake --build --preset vs2026-qt611-debug --target HaiAISpecMonitor DetectionLab scn_tests scn_session_tests scn_policy_tests scn_policy_storage_tests
ctest --preset vs2026-qt611-debug
git diff --check
```

已注册 `scn_algorithm`、`scn_channel`、`scn_pipeline`、`scn_source`、`scn_hash`、
`scn_session`、`scn_policy`、`scn_policy_storage` 八组测试。
测试使用隔离的测试后端，不访问 GPU 或设备，不代表真实模型兼容性或检测精度通过。
本轮实施进行代码检查和编译，CTest、模型推理、文件/UI 及硬件验收由用户执行。

## 运行时状态

程序底部状态栏显示 CPU、GPU、RAM、当前频率范围、RBW、采集时间、当前数据源、
BB60C 接入状态、海得罗捷接入状态、设备状态、网络状态和自检结果。

SCN 状态另外显示累积数量、处理耗时、结果延迟及丢帧数；详细信息包含模型标识、
处理帧号和耗时分位数。GPU 检测可以低于显示帧率，界面显示最近完成的兼容结果，
不会把它误标成当前原始帧的即时检测。算法接口及三种数据源保持分层隔离。
