# FFSCN 17 阶 TensorRT 检测后端接入方案

## 目标与范围

在当前 `DetectionEngine` 中增加可切换的 FFSCN 后端，并将 **SCN / FFSCN（17 阶）** 放入系统设置。SCN 继续作为默认值，两种算法共用采集、跟踪、白名单、告警和显示流程。本期使用已完成训练的固定权重，不重新训练；将 PyTorch 权重导出为 ONNX，构建并验证 TensorRT engine。

FFSCN 检测语义为“最近 10 个实际处理谱帧组成的窗口中出现过该信号”。该语义会在状态和检测详情中标明。模型频率轴为 17 阶，即最大输入宽度 `2^17 = 131072`；动态 engine 同时支持 `2^13` 至 `2^17` 的 2 的幂宽度。

## 当前 SCN 检测逻辑梳理

根据当前 `algorithm/DetectionEngine/DetectionEngine.cpp`、`algorithm/preprocess`、`algorithm/detector`、`algorithm/refine`、`algorithm/fusion` 和 `algorithm/tracking` 实现，数据流为：

```text
BB60C / Harogic / File 原始 dBm 功率谱
  → 最近 16 个实际处理帧
  → dB 算术平均谱与逐点最大谱
  → 两个分支分别切成 32768 点窗口，默认步长 16384
  → 每个窗口 min/max 归一化；不完整尾窗按归一化零补齐
  → SCN TensorRT 推理
  → 三点局部峰值、TopK、中心/带宽/偏移解码、NMS
  → 原始功率谱 CNR 筛选
  → 窗口与平均/最大分支融合
  → 信号关联、边界稳定、稳定频段电平重测
  → 白名单、告警、信号表与检测标记
```

配置默认值包括 SCN 置信度 `0.1`、NMS IoU `0.5`、TopK `200`、每窗口候选上限 `150`、CNR 门限 `3 dB`。SCN 原有 QSettings 键继续读取，旧安装中已保存的值不被覆盖。频率映射直接使用原始 PSD 网格，不重算 FFT，也不使用绘图降采样数据。

历史工程已有频段重叠、中心距离、带宽比关联，以及中位数、EMA 和突变确认的边界稳定逻辑。FFSCN 后端复用这套 tracker 和业务处理，不另建一套信号 ID 或告警实现。关于 FFSCN 改良检测效果的历史讨论见 [Codex 会话](codex://threads/01a0c29c-237d-72c1-bacb-ca579dbbb4b0)。模型检测改善程度需基于同源谱数据对照测量；增加输入点数本身不会提升仪器的 RBW 或分辨能力。

## 固定模型与 TensorRT 转换

使用共享训练项目中已完成的权重：

```text
\\192.168.1.100\huanghao\coding\FFSCN_train2\runs\NFFT17_down13_resume_epoch83\train_4\weights\model_epoch_0155.pth
SHA-256 7ec171094decd4557abaaf3e295b0fd7397196c67a9249d5593aef00db885da6
```

共享训练项目只读。权重信息为训练完成、epoch 155、17 阶 NFFT、`down_nums=13`。该文件作为固定基线，不称为经过独立验证的最佳 checkpoint。

| 项目 | 导出约定 |
|---|---|
| 网络 | `down_nums=13`、MobileNetV3 Large、FPN、neck channels 64、scale 4 |
| 输入 | `spectrum`，float32，`[1,1,10,N]` |
| 输出 | `hm`、`bw`、`off`，float32，`[1,1,1,N/4]` |
| 动态宽度 | 8192、16384、32768、65536、131072 |
| ONNX | opset 17；导出仅包含网络前向，标准化、解码和 NMS 留在 C++ |
| 初始参数 | confidence `0.7`、NMS IoU `0.3`、TopK `512`、每窗口候选上限 `512` |

转换程序为 `tools/export_ffscn_onnx.py` 与 `tools/build_ffscn_engine.ps1`。导出器严格校验 checkpoint SHA、网络输出形状和有限值，随后对 8192 与 131072 两种输入宽度检查 PyTorch/ONNX 的三路输出，目标为 `atol=1e-4, rtol=1e-3`。TensorRT 使用本机 RTX 5060 Laptop GPU 与 TensorRT 10.11 构建关闭 TF32 的 FP32 基准 engine 和 FP16 候选 engine。FP16 只有在固定样本的解码候选一一匹配、边界误差不超过一个原始 bin、分数差不超过 `0.01` 后才可部署；否则使用 FP32。

部署端只加载 engine，不依赖 Python、ONNX Runtime 或 TensorRT builder。engine、ONNX、checkpoint 哈希、模型契约、profile、TensorRT/CUDA/GPU 与转换验证信息写入 `models/ffscn_17.manifest.json` 及转换报告。engine 与 CUDA/TensorRT 运行库匹配具体 GPU 和 TensorRT 构建平台；更换部署平台或 GPU 时需重新生成并复核 engine。

## FFSCN 输入与检测链路

```text
实际收到的原始 PSD 帧
  → 按时间顺序组成最近 10 帧矩阵
  → 映射至 FFT 频宽窗口
  → 对完整 10×N 矩阵按均值与样本标准差标准化
  → 动态形状 FFSCN TensorRT 推理
  → 独立解码、跨窗口频率 NMS、原始 PSD CNR 与电平测量
  → 公共跟踪与业务流程
```

- 输入取检测线程实际收到的原始 PSD，不读取 UI 瀑布图。前 9 帧处于 `WarmingUp`，不复制谱行凑足时间维，不进行推理、轨迹漏检判定或告警命中计数。
- 原始谱短于 131072 点时，选取与原始点数距离最近的受支持 `2^N` 宽度，`13 ≤ N ≤ 17`；距离相同时取较大的宽度。对整条频率轴线性插值至该宽度，并按新频点间隔映射回原始频率覆盖范围。少于 8192 点时取 8192 点。短谱不再零填充；插值不会增加仪器 RBW 或频率分辨率。
- 原始谱等于或长于 131072 点时使用 131072 点窗口、步长 65536；尾窗贴齐原始频谱末端，并跳过重复的窗口起点。不进行重采样或伪造频点。
- 10 行均按时间从旧到新排列；每接收一个有效帧即构成一个新窗口。缺帧保留真实时间间隔并记录序号缺口，不插入虚构谱行。
- 标准化采用整个矩阵的 `(x-mean)/sample_std`，与训练输入约定相同。常量矩阵归零，NaN/Inf 输入拒绝处理。

### 解码与电平测量

- 解码独立于 SCN：7 点局部峰值、严格大于置信度门限、按分数排序的 TopK、stride-4 坐标恢复；NMS 采用分数优先的贪心过程，被抑制候选不能继续抑制其他候选。
- 候选边界经各频率窗口的频率网格映射到原始绝对频率。跨窗口使用频率 NMS 去重，保留最高分候选的边界，不套用 SCN 的区间并集融合。
- 每个候选在 10 行原始 PSD 上独立测量 CNR，取 CNR 最高的行作代表；该行同时提供信号电平、噪声电平和 CNR，记录对应谱行时间。默认门限仍为 `3 dB`。稳定带宽也在同一 10 行上重新测量。
- 跟踪 ID 和稳定边界共用现有实现。FFSCN 的 `lastSeen` 对应窗口结束时间，出现次数是命中窗口数，并非发射次数。结果附带窗口起止时间、实际帧缺口、模型信息、推理时间和测量谱行时间。

## 接口、设置和部署行为

- `DetectionConfig` 增加 SCN/FFSCN 后端枚举和独立 `FfscnConfig`。SCN 原配置保留；FFSCN 路径和参数保存于 `detection/ffscn/*`，选择保存于 `detection/backend`。未设置该键时仍用 SCN，兼容旧配置。
- `DetectionEngine` 只为当前选中后端执行推理；SCN 原来的 32768 点输入布局和张量名称保持不变。FFSCN engine 使用动态 `[1,1,10,N]` 输入与 `hm/bw/off` 输出。
- 系统设置包含后端选择、两种 engine 独立路径和各自解码参数。下拉框只改草稿，“应用检测设置”被接受后才保存。
- 更换后端、模型、GPU 或检测开关要求先停止监测；接受新配置时清除旧检测、轨迹和业务状态。加载失败报告所选后端与错误，原始频谱绘制仍可继续。
- `WarmingUp` 状态展示 `n/10`，不计入完成推理吞吐，不送入 `PolicyEngine`。后端运行信息显示预热进度、窗口覆盖时间、缺帧数量、模型哈希/信息和推理耗时。
- CMake、Release 安装程序和依赖归档一并部署 `scn_model.engine`、`ffscn_17.engine` 及 FFSCN manifest；构建工具、Python 和 ONNX Runtime 不进入产品运行包。

## 实施与验收顺序

1. 锁定模型来源和接口契约，维护本计划文档。
2. 导出 ONNX 并完成 PyTorch/ONNX 固定宽度与动态宽度数值检查。
3. 构建 FP32 与 FP16 TensorRT engine，核对 TensorRT 输出和解码差异，选择部署 engine。
4. 实现短谱插值、长谱分窗、10 帧预热、FFSCN 解码、同帧 CNR 测量与事务性回滚。
5. 增加设置持久化、模型切换状态重置、状态显示、DetectionLab 导出与包部署检查。
6. 使用同一原始 PSD、频率网格与时间轴对比 SCN/FFSCN，报告检测和性能指标；实测前不改变默认后端。

| 验收层级 | 验收项 |
|---|---|
| 转换 | strict checkpoint load；ONNX/TensorRT I/O 名称、形状、dtype 和有限值；FP32 数值容差；FP16 候选与解码边界比较 |
| 算法 | 插值宽度与频率范围、矩阵标准化、10 帧顺序、预热、尾窗、跨窗 NMS、CNR 代表帧、取消回滚、配置迁移和后端切换隔离 |
| 效果 | Precision、Recall、F1、频率 IoU、中心/带宽误差、ID 切换、边界抖动、首次检出延迟、离场拖尾 |
| 性能 | 处理 P50/P95、显存、吞吐、队列深度与丢帧；效果标注按“窗口中曾出现”定义，模型互相不能充当真值 |

长时 CTest、UI 操作、实际仪器和同源数据效果验收由使用方执行；执行结果需在验收记录中区分已验证、未验证与环境受限项。

## 当前实现记录

- 已完成：后端选择与系统设置持久化、FFSCN 10 帧输入和预热、最近 2^N 宽度插值、长谱分窗、独立解码和 CNR 测量、共享跟踪、检测状态显示、DetectionLab 导出、CMake 与打包部署接入。
- 已完成模型产物：固定 checkpoint 严格加载并导出 ONNX；8192 与 131072 点的 PyTorch/ONNX 三路输出检查通过；TensorRT 10.11 为 RTX 5060 Laptop 构建了 FP32 和 FP16 engine。当前部署的是 FP32，文件为 `models/ffscn_17.engine`，SHA-256 为 `dd88ab057d5af35e412447428de1a0163f79d0d7ac6a5c1a86308b898d50fd81`。
- 已验证 C++ TensorRT 后端加载和 10 帧端到端推理：10000 点原始谱插值到 8192 点、131072 点输入均成功，三个输出有限且形状正确。8192 点结果的三路输出均符合 `atol=1e-4, rtol=1e-3`；131072 点时，TensorRT 对 ONNX Runtime 的原始 `hm` 有 263/32768 个值、`bw` 有 1/32768 个值超出该阈值，最大绝对误差分别为 0.0010616 和 0.35352，`off` 全部通过。该合成窗口的解码结果仍为相同两个峰，边界最大差 0.026 个原始 bin、置信度一致。该数值差异尚未在有标注的实际 PSD 数据上复核，不能据此声称整体精度等价。
- FP16 候选没有选为部署版：同一合成窗口上 FP32 解出 2 个候选，FP16 解出 3 个，不符合候选一一匹配条件。
- Debug 下 CMake 配置及应用、DetectionLab 和测试目标均已编译成功。尚未执行 CTest、UI 交互、实仪采集、带真值的 SCN/FFSCN 效果对照和长时性能验收；这些结果须后续补记。完整单次模型转换与运行检查记录见 `models/ffscn_17_export_report.json`、`models/ffscn_17_runtime_validation.json` 和 `models/ffscn_17.manifest.json`。
