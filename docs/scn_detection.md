# SCN 检测开发与验收说明

本文件记录 `scn_dev` 实现。`references.md` 是架构参考，保持原样。
算法依据参考任务“梳理BB60C SCN检测流程”及其 `BB60C_detect` 实现，
复用 ISA 的 `scn_model.engine`，不移植旧 ISA 的长短时最大谱/当前帧边界复核流程。

## 数据与检测语义

- 输入是原始 dBm 功率谱，不做 FFT，不读取绘图包络。
- `i` 点频率为 `start + i*binWidth`；上边界为 `start + N*binWidth`。
- 最近 16 个实际处理的有效帧按 dB 算术平均和逐点最大值累积，首帧即检测。
- 每个分支使用 32768 点窗口、16384 点步长，尾窗以有效区域最小值补齐。
- min/max 归一化的常量窗为零；填充区禁止产生有效检测。
- SCN 输出以热图三点局部极大值、TopK=200、置信度 >=0.4、stride=4 解码。
- NMS 抑制 IoU >=0.5 的低分候选，每窗最多 150 项；同分按索引升序保证确定性。
- 原始候选先按有效范围裁剪，再转整数 bin；区间采用 `[begin,end)`。
- CNR 信号电平取中间约 60%，噪声取两侧各约 10% 的有效邻域（至少一个点）；
  两侧无样本时 CNR=0。默认阈值 3dB；阈值 <=0 明确表示关闭 CNR 筛选。
- 各分支先跨窗融合，再双分支融合。按频率排序，与最后一个已融合区间比较，
  保持参考 Python 的顺序语义，并非图连通分量聚类。
- 融合条件是 IoU >=0.1 或交集/较窄宽度 >=0.5；`gapHz=0` 关闭间隔融合。
- 融合保留并集边界、最大置信度/信号电平和最小噪声电平，并记录 Average/Maximum/Both。
  这些是融合后的观测指标，不是对并集频段重新积分得到的功率。
- 默认最多 4096 项；截断时保留高置信度结果并记录数量。`snrDb` 兼容字段表示 CNR。

跟踪使用 overlap/min-width >=0.45 的一对一匹配，按得分/中心距离/ID 决定顺序。
输出保留当前观测边界，不做 EMA。1 秒内未匹配轨迹仅用于身份重连，不显示成当前信号；
密集匹配按每个观测最多 64 条边分批缓存、堆合并，耗尽后补批；不截断合法匹配边。
更新在工作副本上完成，取消或异常不提交身份变化。
次数指检测观测命中次数，不是采集帧总数，也不是 UI 刷新次数。
新监测、源/频率网格/参考电平变化、回放跳转和循环重启时清空历史与 ID。
无效帧不进入累积，取消/异常推理回滚当前帧的累积变更。

## 线程与状态

Source QThread 只读取数据、执行源控制；算法 std::thread 独占 DetectionEngine/CUDA。
二者通过容量 8 的有界通道传递不可变帧；硬件满队列丢旧帧并计数，FILE 暂停生产。
FILE 循环前等待末尾所有检测完成，再进入新轮次，不能通过清队列跳过文件尾。
Qt 控制槽不等待通道空间；普通停止立即使旧轮次失效，不在 UI 等待 GPU 整谱计算。
销毁应用时才等待线程和已提交 GPU 工作退出。

33ms 发布最新原始谱及最近完成、同轮次/网格的结果。检测结果自带独立帧号与时间，
结果更新不重复推进瀑布或 UI 最大/平均谱。显示 100 帧历史与检测 16 帧历史互不影响。
SCN 状态提示记录检测完成吞吐、P50/P95、队列深度、丢帧以及 UI 原始帧提交速率。
UI 提交速率不等于显卡实际呈现 FPS；Direct2D 呈现性能需在运行时配合分析工具验收。
文件时间采用从文件起点算起的 `帧索引/fps`，与执行速度无关；不等价于原始采集绝对时间。

配置为强类型 `DetectionConfig`。相同配置不变更状态；阈值更新下一周期生效并清除旧身份，
窗口长度变更重置累积；更换 engine/GPU 或切换检测开关必须停止后重新初始化。
`Bypassed` 表示关闭/等待；`Accumulating` 表示有结果但未满 16 帧；`Completed` 表示满窗结果；
`Error` 包含失败原因；`Cancelled` 不发布到新轮次。

模型错误不停止原始频谱显示、不产生虚假检测，也不自动切换 ONNX/Python/CPU 检测器。
终态诊断导出失败保留成功检测，另设 `exportFailed` / `diagnosticError`；DetectionLab 仍返回失败码，
不把不完整的导出视为成功。文件循环只重置历史，不反复加载失败模型；显式重新开始/改配置可重试初始化。
算法层不实现分类、白名单或告警；应用层在检测结果之后独立执行白名单结果整理和告警规则。
信号类型仍为“未分类”，告警等级由规则条件决定，不由置信度自动映射。白名单不抑制告警。

## 应用层白名单与告警

白名单和告警规则在应用层处理，频段统一按相交匹配，端点相接也命中：
`signalEnd >= policyStart && signalStart <= policyEnd`。每条命中的白名单生成一条固定配置频段的
业务结果，并移除本次命中的原始结果；多个原始结果命中同一白名单时合并为一条，多个白名单命中
同一原始结果时分别生成结果。替换结果沿用信号电平最高的代表检测值，并保留全部原始 ID。
ISA JSON 导入/导出和应用。配置保存到 `config/policy.json`，事件历史保存到
`config/policy.sqlite`；历史页面支持查询、确认和 CSV/JSON 导出。规则状态按“业务来源 × 业务 ID ×
规则”维护，活动事件取最高等级，暂停、故障和结果截断不会被误判为信号消失。

## 模型与依赖

默认 engine：`D:/project/isa/bin/models/scn_model.engine`。
默认 SDK：`D:/project/isa/submodules/HaiSignal/3rdparty/tensorrt`（10.11.0）。
输入为 `resnet_32_input:0`，float32 `[1,32768,1,1]`。
输出命名绑定：`Identity:0` 带宽、`Identity_1:0` 热图、`Identity_2:0` 中心偏移，
每项为 8192 个 float32，不能按枚举顺序或 Python 输出顺序接线。

实现检查名称、方向、shape、stride、设备位置与 dtype；内部权重 FP16 不意味着 I/O 是 FP16。
使用一个 execution context/stream，固定设备及页锁定传输缓冲，窗口 batch 固定为 1。
首次推理包括 TensorRT/CUDA 惰性初始化，因此首帧时延不能代表稳定吞吐。
错误日志包含 SHA256、实际 TensorRT/CUDA/GPU 和张量元数据；不自动重建 engine。
若 GPU/engine 不兼容，需由用户提供同一模型对应的兼容 TensorRT engine。

CMake 仅启用 CXX。FindCUDAToolkit 可以查询 nvcc 版本，但不会用 nvcc 编译代码。
部署使用 TensorRT/bin 的 nvinfer_10.dll、cudart64_12.dll、cublas64_12.dll、cublasLt64_12.dll；
包内 cudart 是 12.9，不得被 Toolkit 12.4 的同名 DLL 覆盖。
模型与 DLL 复制到各运行目录，模型位于 `models/scn_model.engine`；SDK/模型/导出不提交 Git。

## 编译与运行（用户执行）

```powershell
cmake --preset vs2026-qt611-debug
cmake --build --preset vs2026-qt611-debug --target HaiAISpecMonitor DetectionLab scn_tests scn_session_tests scn_policy_tests scn_policy_storage_tests
ctest --preset vs2026-qt611-debug
git diff --check

$lab = '.\out\build\vs2026-qt611-debug\tools\DetectionLab\Debug\DetectionLab.exe'
$data = 'D:\project\isa\bin\data\spectrum_data_org\20260911_152444_651_Fc=2025000000_Bw=3950000000_Rbw=50000_Reflevel=-20.0_SpectrumLen=202242.dat'

# 只加载、校验 engine，不推理
& $lab --inspect-model

# 导出默认参数（不访问 GPU），可以编辑后传给 --config
& $lab --write-config '.\out\scn-config.json'

# 前32帧：覆盖首帧、满16帧、旧帧滑出
& $lab --file $data --frames 32 --fps 30 --output '.\out\scn-results.jsonl' --csv '.\out\scn-signals.csv'

# 单帧与跳转：Enter 下一帧，seek 100 清空状态后跳到第100帧，q 退出
& $lab --file $data --step

# 从100帧空历史开始，按需导出原始/累积谱、各窗输入输出、候选和融合结果
& $lab --file $data --start-frame 100 --frames 2 --dump '.\out\scn-stages' --output '.\out\scn-debug.jsonl'

# 与同一帧区间的已有 JSONL 对照；写出单独的坐标/CNR/置信度差异记录
& $lab --file $data --frames 32 --output '.\out\scn-repeat.jsonl' --compare '.\out\scn-results.jsonl'
```

所有文件帧索引从 0 开始；`--frames 0` 表示处理剩余全部文件。工具不循环文件、不丢帧，
fps 只定义信号时间，不限制离线执行速度。无文件名元数据时可传 `--points`、`--center-hz`、
`--span-hz`、`--reference-dbm`。JSON 配置使用与结构体对应的嵌套对象，见 `--write-config` 输出。

JSONL 每处理帧一行（包括零检测），CSV 每观测一行（无检测帧没有信号行）。
`*.manifest.json` 记录文件、逻辑帧率、配置、模型哈希与环境。纳秒/序号在 JSON 中以字符串保存。
每个 JSONL/CSV 输出各有配套 manifest；`--dump` 在指定目录内创建独立 `run-UUID` 子目录，
内含 `manifest.json`。所有输出（含 `--write-config`）只允许创建新文件，已有路径明确拒绝覆盖；
重复执行请使用新输出名。manifest 记录运行配置，不代表整个运行已完成；失败时可能保留部分产物。
阶段 `.f32` 为连续 little-endian float32，无额外头；相邻 JSON 记录窗口起点、有效长度与候选。
`*.comparison.jsonl` 以 IoU>=0.5 贪心配对，列出未匹配数、边界 bin 误差、CNR/置信度差值；
这是定位差异的报告，不自动宣称精度通过。同分 TopK 和阈值附近的浮点差异需单独检查。
参考 JSONL 必须含有效检测阶段、检测数组及有限且合法的区间，不接受错误帧或缺字段记录冒充零检测。
只用少量帧启用 --dump，避免每窗原始张量导出影响吞吐和占用大量磁盘。

## 验收清单

1. CPU 六组 CTest 通过（含 Qt 会话集成）；该结果不代表真实模型可运行。
2. --inspect-model 显示正确 engine 哈希、GPU、IO 名称与尺寸；错误模型/路径给出具体原因。
3. 默认202242点数据每分支12窗、每周期共24窗；首帧1/16，17帧后窗口应为2～17。
4. 相同窗口输入对照 ISA TensorRT 原始输出，再以相同输出对照参考 Python 解码/CNR/融合。
5. 边界、尾窗和小于32768点的输入没有填充区伪信号，已知频点偏差可用bin单位核查。
6. 文件重复执行稳定ID/次数/逻辑时间可复现；Seek、循环、切源、重新开始不混入旧状态。
7. 连续文件/BB60C采集时滚轮、框选、拖拽仍响应；结果比原始谱滞后时如实显示延迟。
8. 最大谱/平均谱的100帧UI开关不影响算法16帧结果；检测单独完成时瀑布不重复添加行。
9. 模型损坏/设备不可用/禁用检测时原始绘图可用；不出现假分类或假告警。
10. Release 记录检测吞吐、P50/P95（最近最多2048次）、队列深度、结果延迟、丢帧和内存稳定性。

## 本轮交付检查（2026-09-22）

VS 2026 Debug / Release 均已编译 `HaiAISpecMonitor`、`DetectionLab`、`scn_tests`、
`scn_session_tests`；`git diff --check` 无错误，CTest 仅使用 `-N` 确认六组注册。
PE 依赖检查确认 CPU 测试不加载 TensorRT/CUDA/BB60C；会话测试额外依赖 Qt Core。

两个主程序目录部署的 engine 与 ISA 原文件 SHA256 一致：
`ae89d04332758d466846b71afc849c446500a82a8bd74fd8c671927247746627`。
部署的 cudart DLL 版本来自 SDK 的 12.9，而不是 Toolkit 12.4。

Qt 部署工具仍提示可选 `dxcompiler.dll/dxil.dll` 未找到以及 `VCINSTALLDIR` 未设置；
构建未因此失败。运行期部署、真实 engine 兼容性和检测准确性不能由编译成功替代。
本轮未执行 CTest、模型推理、UI 播放或硬件测试。
