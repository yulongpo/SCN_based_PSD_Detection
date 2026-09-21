
如果完全忽略现有 `RadioAI Flow / Component / HQSigMF` 检测框架，只保留现有 Qt UI 和操作习惯，我认为最佳方案不是“重新做一个更简单的 Flow”，而是直接建立一个**以 DetectionEngine 为中心的单机实时检测架构**。

核心原则可以浓缩为：

> **数据源负责稳定地产生数据；DetectionEngine 对检测结果负全部责任；UI 永远不参与算法计算，只消费最新状态和结果。**

## 1. 我建议直接采用“三层 + 一个旁路”

新的 ISA2 可以只保留三个主层次：

```text
Presentation
    Qt UI
       │
       │ command / snapshot
       ↓
Application
    MonitoringSession
       │
       ├──────── Source
       │
       ↓
Algorithm
    DetectionEngine
```

另外增加一条专门服务算法研发的：

```text
DetectionEngine
      ↓
Diagnostics / Debug Tap
      ↓
算法可视化 / Dump / Replay
```

这里最重要的变化是：

```text
旧设计：
Component 是系统中心

新设计：
DetectionEngine 是系统中心
```

UI、设备、文件全部围绕 DetectionEngine 服务。

---

# 2. 数据源应该彻底统一

只保留三种：

```text
BB60C
Harogic
File
```

它们都实现同一个非常简单的接口：

```cpp
class ISpectrumSource
{
public:
    virtual bool open(const SourceConfig&) = 0;
    virtual bool start() = 0;
    virtual void stop() = 0;

    virtual bool read(SpectrumFrame& frame) = 0;
};
```

然后实现：

```text
BB60CSource
HarogicSource
FileSpectrumSource
```

但 DetectionEngine 完全不知道当前是什么设备。

它只看：

```cpp
struct SpectrumFrame
{
    uint64_t sequence;

    int64_t timestamp_ns;

    int64_t start_frequency_hz;
    double bin_width_hz;

    int32_t point_count;

    SpectrumBuffer spectrum;

    SourceInfo source;
};
```

这里我特别推荐：

```text
start_frequency_hz
+
bin_width_hz
+
point_count
```

而不是算法内部一直使用：

```text
fc
bw
len
```

因为前一种形式对于实际频谱点坐标更明确：

$$
f_i=f_{start}+i\Delta f
$$

以后设备输出频点数量、扫宽或者 RBW 有变化，算法不需要猜测频率映射关系。

---

# 3. DetectionEngine 应成为整个项目真正的核心

建议最终只有一个主要算法入口：

```cpp
class DetectionEngine
{
public:
    bool initialize(const DetectionConfig& config);

    DetectionResult process(
        const SpectrumFrame& frame);

    ApplyConfigResult updateConfig(
        const DetectionConfig& config);

    void reset();
};
```

算法工程师以后最主要调试的函数就是：

```cpp
DetectionEngine::process()
```

进去以后一路能够看到：

```text
process
  │
  ├─ preprocess
  ├─ accumulation
  ├─ noise estimation
  ├─ detector inference
  ├─ refine
  ├─ fusion
  ├─ tracking
  └─ build result
```

而不是：

```text
DLL
 ↓
Flow
 ↓
HQSigMF
 ↓
另一个 DLL
 ↓
Flow
 ↓
另一个 HQSigMF Section
```

这是此次重构最重要的收益。

---

# 4. DetectionEngine 内部仍然必须模块化

算法中心化不意味着写成一个巨大的 `.cpp`。

正确结构应该类似：

```text
algorithm/
│
├── DetectionEngine.h/.cpp
│
├── DetectionConfig.h
│
├── DetectionResult.h
│
├── SpectrumFrame.h
│
├── preprocess/
│   └── SpectrumPreprocessor
│
├── accumulation/
│   ├── SpectrumAccumulator
│   └── NoiseFloorEstimator
│
├── detector/
│   ├── IDetectorBackend
│   └── ScnDetector
│
├── refinement/
│   └── DetectionRefiner
│
├── fusion/
│   └── DetectionFusion
│
├── tracking/
│   └── SignalTracker
│
└── diagnostics/
    └── DetectionDiagnostics
```

区别在于：

> 这些是 **C++ 模块边界，而不再是 DLL/Flow/Port 边界**。

也就是：

```text
高内聚
+
低部署复杂度
+
仍然保持清晰模块化
```

---

# 5. 强烈建议算法使用自己的数据结构

不要让新 DetectionEngine 内部继续使用 `HQSigMF`。

否则旧架构虽然删掉了，旧架构的数据耦合仍然存在。

算法内部应该明确区分：

```text
SpectrumFrame

DetectionCandidate

RefinedDetection

FusedDetection

Track

DetectedSignal
```

例如：

```cpp
struct DetectionCandidate
{
    double start_hz;
    double end_hz;

    float confidence;
    float snr;

    int model_class;
};
```

最终：

```cpp
struct DetectedSignal
{
    int64_t id;

    double start_hz;
    double end_hz;
    double center_hz;
    double bandwidth_hz;

    float snr;
    float confidence;

    TrackState state;
};
```

这种强类型设计对于算法调试非常重要。

---

# 6. UI 流畅性的关键不是“算法快”，而是完全解耦

这次重构我建议明确规定：

> **任何设备读取、文件读取、CUDA 推理、频谱累积、Detector、Tracker 都不允许运行在 Qt UI Thread。**

至少采用三个独立执行域：

```text
Thread 1
Qt UI Thread

Thread 2
Spectrum Source Thread

Thread 3
Detection Engine Thread
```

推荐再增加一个非关键线程：

```text
Thread 4
Debug / Recorder / Disk IO
```

数据通路：

```text
BB60C / Harogic / File
          │
          ↓
     Source Thread
          │
     SPSC Queue
          │
          ↓
    Algorithm Thread
          │
     Result Queue
          │
          ↓
      Qt UI Thread
```

Qt 主线程绝不等待 Algorithm Thread。

---

# 7. UI 应采用“最新快照”思想，而不是“每包必画”

这是保证流畅性的关键。

例如硬件可能：

```text
100 FPS
300 FPS
甚至更高
```

UI 没有必要逐帧显示。

建议 UI：

```text
30 FPS
或
60 FPS
```

固定刷新。

比如：

```cpp
QTimer renderTimer;
renderTimer.start(33);
```

每 33 ms：

```text
读取最新 DisplaySnapshot
↓
刷新 Spectrum
↓
刷新 Waterfall
↓
更新 Detection Overlay
```

如果算法期间产生了：

```text
frame 100
frame 101
frame 102
frame 103
```

UI 可能直接拿：

```text
frame 103
```

而不是：

```text
100 → 101 → 102 → 103
```

否则 UI 很容易形成积压。

这和你现在 CollMonitor 的 ~30 FPS 思路是一致的，但新架构应该把这个原则贯彻到整个系统。

---

# 8. UI 数据和算法数据不要使用同一套对象

建议 DetectionEngine 输出两类数据：

```text
DetectionResult
```

和：

```text
DisplaySnapshot
```

前者是精确算法结果：

```text
全部 signal
全部 tracking state
真实时间戳
算法状态
```

后者专门给 UI：

```cpp
struct DisplaySnapshot
{
    uint64_t sequence;

    SpectrumView spectrum;

    std::vector<SignalView> signals;

    DetectionProgress progress;

    PerformanceStats performance;
};
```

这样可以：

```text
算法数据保持精确
UI 数据保持轻量
```

UI 不需要知道 Detector 的内部候选，也不应该直接修改算法对象。

---

# 9. 数据通路尽可能避免大频谱反复复制

宽带频谱的真正性能风险往往不是 Qt，而是：

```text
几百 k / 几 M 点 float
×
多个模块
×
多次 memcpy
```

新设计建议使用：

```text
BufferPool
+
Immutable Frame
+
move / shared ownership
```

例如：

```cpp
using SpectrumBuffer =
    std::shared_ptr<const SpectrumBufferData>;
```

数据源从 BufferPool 拿内存：

```text
Source
 ↓
fill
 ↓
SpectrumFrame
 ↓
DetectionEngine
```

算法完成以后归还池。

不要每走一步：

```cpp
std::vector<float> copy = old;
```

对于长窗、短窗等内部状态，DetectionEngine 自己管理专用内存。

---

# 10. 队列必须是有界的

不要使用无限：

```cpp
QQueue<SpectrumFrame>
```

否则算法偶尔慢下来，最终就是：

```text
数据不断积压
↓
内存上涨
↓
UI 显示几秒前的数据
```

建议：

```text
Source → Algorithm

bounded SPSC ring buffer
```

例如容量：

```text
8 / 16 / 32 frames
```

并根据运行模式定义两种策略。

### 实时设备模式

BB60C / Harogic：

```text
队列满
↓
允许丢旧帧
↓
优先保持实时性
```

同时统计：

```text
dropped_frame_count
```

Tracker 使用真实时间戳，所以不能以“固定帧间隔”假设处理。

### 文件调试模式

File：

```text
绝不丢帧
↓
producer 等待
```

这样才能得到：

```text
100% 可重复
100% 可复现
```

这一点非常适合算法调试。

---

# 11. 文件流应该升级成“算法实验入口”

我认为 File Source 不应该只是模拟设备。

它应该成为整个系统的：

> **Deterministic Replay Engine**

支持：

```text
正常播放
暂停
单帧
前进 N 帧
实时速度
2× / 5× / 最大速度
Seek
指定 frame
指定时间
```

这样算法工程师遇到问题：

```text
Frame #1827 检测错误
```

可以：

```text
Seek 1820
↓
Single Step
↓
1821
↓
1822
...
↓
1827
```

然后 VS 直接断点进去。

这会比现在整个 Flow 调试高效很多。

---

# 12. 算法可视化应该成为第一等公民

这是我认为此次重构除了 DetectionEngine 之外最值得投入的一项。

DetectionEngine 内部增加：

```cpp
DetectionDiagnostics
```

但默认关闭。

启用 Debug Mode 后可以观察：

```text
Input Spectrum

Long Accumulation

Short Accumulation

Noise Floor

Raw Model Detection

Refined Detection

Fusion Result

Tracker Input

Association Result

Final Track
```

例如点击 UI 中某个 `Track #125`，算法调试面板可以显示：

```text
Track #125

Current measurement
2400.3 ~ 2420.6 MHz

Detector
2399.8 ~ 2421.0 MHz

Refiner
2400.2 ~ 2420.7 MHz

Fusion
2400.2 ~ 2420.7 MHz

Previous Track
2400.5 ~ 2420.4 MHz

Match score
0.87

Final
2400.4 ~ 2420.5 MHz
```

这样才叫真正的“算法可视化”。

---

# 13. Debug Tap 不应该拖慢正式系统

不要让每个算法阶段一直复制所有数据。

建议：

```cpp
enum class DebugTap
{
    InputSpectrum,
    LongSpectrum,
    ShortSpectrum,
    NoiseFloor,
    RawDetection,
    RefinedDetection,
    Fusion,
    Tracking
};
```

运行时：

```cpp
debug.enable(DebugTap::RawDetection);
debug.enable(DebugTap::Tracking);
```

没有打开的 Tap：

```text
几乎零成本
```

打开以后才生成快照。

对于大频谱还可以：

```text
每 N 帧采样
```

或者：

```text
用户按下 Capture
↓
抓下一次完整算法过程
```

这比全程 dump 文件好得多。

---

# 14. 参数系统也应该彻底重做

不要再使用：

```text
"name": "iou"
"string_val": "0.1"
```

这种字符串参数。

统一采用：

```cpp
struct DetectionConfig
{
    AccumulatorConfig accumulator;
    DetectorConfig detector;
    RefineConfig refine;
    FusionConfig fusion;
    TrackerConfig tracker;
};
```

JSON 只作为持久化格式：

```text
JSON
 ↕
DetectionConfig
 ↕
DetectionEngine
```

并且参数分成三种：

| 参数类型          | 行为                        |
| ----------------- | --------------------------- |
| Runtime 参数      | 立即生效                    |
| Reset 参数        | 清空算法状态后生效          |
| Reinitialize 参数 | 重新加载模型/重新初始化 GPU |

UI 修改参数以后 DetectionEngine 应明确返回：

```text
Applied

RequiresReset

RequiresRestart

Invalid
```

而不是偷偷重建内部对象。

---

# 15. 建议加一个明确的 MonitoringSession

UI 不要直接管理：

```text
source
engine
queue
thread
```

增加：

```cpp
class MonitoringSession
```

成为应用层唯一入口：

```cpp
session.setSource(...);

session.start();

session.pause();

session.resume();

session.stop();

session.updateDetectionConfig(...);
```

内部负责：

```text
Source lifecycle

Algorithm lifecycle

Thread lifecycle

Queue reset

Source switching

Error handling
```

所以 Qt Controller 实际只需要面对：

```text
MonitoringSession
```

---

# 16. Source 切换必须做成事务

例如：

```text
BB60C
↓
切换
File
```

不要让 UI 自己零散调用一堆东西。

统一：

```text
Pause display
↓
Stop old source
↓
Drain queue
↓
Reset DetectionEngine
↓
Configure new source
↓
Start source
↓
Resume
```

整个过程由：

```text
MonitoringSession::switchSource()
```

完成。

这样 UI 操作逻辑可以保持不变，但底层会简单很多。

---

# 17. 推荐最终线程模型

这是我认为对目前项目最合适的模型：

```text
                         ┌──────────────────┐
                         │   Qt UI Thread   │
                         │                  │
                         │ Spectrum         │
                         │ Waterfall        │
                         │ Signal List      │
                         └────────▲─────────┘
                                  │
                          latest snapshot
                                  │
                         ┌────────┴────────┐
                         │ Presentation    │
                         │ Snapshot Buffer │
                         └────────▲────────┘
                                  │
                         DetectionResult
                                  │
┌──────────────┐        ┌────────┴─────────┐
│ Source Thread│───────▶│ Algorithm Thread │
│              │ SPSC   │                  │
│ BB60C        │ queue  │ DetectionEngine  │
│ Harogic      │        │                  │
│ File         │        └───────┬──────────┘
└──────────────┘                │
                                │ optional
                         ┌──────▼───────────┐
                         │ Debug / IO Thread│
                         │                  │
                         │ dump             │
                         │ recording        │
                         │ diagnostics      │
                         └──────────────────┘
```

总共：

```text
UI Thread
Source Thread
Algorithm Thread
Optional IO Thread
```

已经足够。

**不建议一开始搞线程池。**

检测算法拥有大量有状态对象，单 Algorithm Thread：

```text
最容易理解
最容易调试
最容易保证顺序
```

内部 GPU 自己实现并行即可。

---

# 18. CUDA / TensorRT 应该完全藏在 DetectionEngine 中

UI 和 Source 都不应该知道：

```text
CUDA Stream
TensorRT Context
GPU Buffer
Host/Device Transfer
```

DetectionEngine 内部可以建立：

```text
GpuContext
│
├── CUDA Stream
├── Workspace
├── TensorRT Context
└── Device Buffers
```

如果之后性能不够，再内部优化成：

```text
H2D
+
Inference
+
Postprocess
```

流水执行。

但外部接口仍然保持：

```cpp
process(frame)
```

不变。

---

# 19. 最推荐的工程目录

如果由我重新组织 ISA2，我会做成：

```text
isa2/
│
├── app/
│   └── HaiAISpecMonitor/
│       ├── ui/
│       ├── controller/
│       └── viewmodel/
│
├── application/
│   ├── MonitoringSession
│   ├── SourceManager
│   └── PresentationModel
│
├── source/
│   ├── ISpectrumSource.h
│   ├── BB60CSource/
│   ├── HarogicSource/
│   └── FileSource/
│
├── algorithm/
│   ├── DetectionEngine
│   ├── types/
│   ├── preprocess/
│   ├── accumulation/
│   ├── detector/
│   ├── refine/
│   ├── fusion/
│   ├── tracking/
│   └── diagnostics/
│
├── runtime/
│   ├── BufferPool
│   ├── SpscQueue
│   └── PerformanceMonitor
│
├── tools/
│   └── DetectionLab/
│
└── tests/
    ├── algorithm/
    ├── source/
    └── integration/
```

这里我尤其建议增加：

```text
tools/DetectionLab
```

---

# 20. DetectionLab 会极大提升算法研发效率

这是一个独立的小程序，不需要完整 ISA UI。

界面类似：

```text
┌─────────────────────────────────────────────┐
│ File      ▶  |  ||  | >| | Frame 1827      │
├─────────────────────────────────────────────┤
│ Input Spectrum                              │
├─────────────────────────────────────────────┤
│ Long / Short Accumulation                   │
├─────────────────────────────────────────────┤
│ Raw Detection / Refine / Fusion             │
├─────────────────────────────────────────────┤
│ Tracker                                     │
├─────────────────────────────────────────────┤
│ Algorithm Timing / Parameters               │
└─────────────────────────────────────────────┘
```

它直接调用：

```cpp
DetectionEngine
```

没有：

```text
完整主界面
设备管理
数据库
告警
其它产品逻辑
```

以后算法工程师 80% 的开发调试都可以在 DetectionLab 完成。

主程序只是验证最终效果。

这会是整个重构最有价值的工具之一。

---

# 21. 正式运行和调试使用完全相同的算法代码

最终应该形成：

```text
HaiAISpecMonitor
        │
        └── DetectionEngine.lib

DetectionLab
        │
        └── DetectionEngine.lib

Unit Tests
        │
        └── DetectionEngine.lib
```

而不是：

```text
产品一套代码

算法调试 Demo 又一套代码
```

这样才能保证：

> **你调试的算法就是产品真正运行的算法。**

---

# 22. 我不建议保留的现有设计

如果按这条路线重构，我会果断移除检测热路径中的以下概念：

* `ComFlow / Port` 算法编排；
* 一个算法阶段一个 DLL；
* `HQSigMF` 作为算法内部数据结构；
* JSON string 参数驱动算法；
* UI 和算法共享复杂业务对象；
* 每一个检测阶段都经过异步组件队列；
* 为了“可扩展”而引入运行时动态算法拓扑。

这些能力并不是完全没价值，而是对于现在的 ISA2 来说，它们带来的复杂度已经明显高于收益。

---

# 23. 最终我会把系统定义成这样

```text
                      ISA2

 ┌──────────────── Presentation ───────────────┐
 │              Existing Qt UI                 │
 │                                             │
 │ Spectrum / Waterfall / List / Settings      │
 └─────────────────────┬───────────────────────┘
                       │
                 DisplaySnapshot
                       │
 ┌──────────────── Application ────────────────┐
 │            MonitoringSession                │
 │                                             │
 │ start / stop / pause / switch / config      │
 └──────────┬─────────────────────┬────────────┘
            │                     │
       SpectrumFrame        DetectionResult
            │                     ▲
 ┌──────────▼───────┐    ┌────────┴───────────┐
 │ Spectrum Source  │    │  DetectionEngine   │
 │                  │    │                    │
 │ BB60C            │    │ Preprocess         │
 │ Harogic          │    │ Accumulate         │
 │ File Replay      │    │ Detect             │
 │                  │    │ Refine             │
 └──────────────────┘    │ Fusion             │
                         │ Track              │
                         └─────────┬──────────┘
                                   │
                             DebugSnapshot
                                   │
                         ┌─────────▼──────────┐
                         │   DetectionLab    │
                         └────────────────────┘
```

## 最终建议

如果目标排序是：

```text
1. UI 始终流畅
2. 算法极易调试
3. 算法过程可视化
4. 三类数据源可靠工作
5. 后续容易替换检测模型
```

那么我认为**最佳方案就是：单体算法库 + 三数据源适配器 + MonitoringSession + 固定帧率 Snapshot UI + 独立 DetectionLab**。

这比重新构造一个轻量 Component/Flow 框架更适合当前项目。

其中最值得首先固定下来的四个核心 API 是：

```cpp
ISpectrumSource
SpectrumFrame
DetectionEngine
DetectionResult
```

只要这四个边界设计正确，后面的 BB60C、Harogic、文件回放、GPU 检测、Tracker 和 Qt UI 都可以比较独立地演进，而不会再次回到现在“改一个检测问题需要穿透多个模块”的状态。
