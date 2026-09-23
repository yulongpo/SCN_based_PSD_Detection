# DetectionLab

与监测界面共用 `FileSource`、`DetectionEngine` 和 ISA TensorRT engine，
用于模型检查、无丢帧离线检测、单帧/Seek 复现和阶段数据导出。

```powershell
cmake --build --preset vs2026-qt611-debug --target DetectionLab
$lab = '.\out\build\vs2026-qt611-debug\tools\DetectionLab\Debug\DetectionLab.exe'
& $lab --inspect-model
& $lab --write-config '.\out\scn-config.json'
& $lab --file 'path/to/spectrum.dat' --frames 32 --output '.\out\results.jsonl'
```

`--inspect-model` 只初始化真实 engine；`--write-config` 不访问 GPU。
`--start-frame` 为从 0 开始的帧索引，起点采用空历史渐进累积；`--frames 0` 处理剩余全部帧。
`--fps` 定义文件逻辑时间，不限制离线处理速度。`--step` 下 Enter 前进、`seek N` 跳转、`q` 退出。

`--output` 导出每帧 JSONL（包含原始与稳定跟踪结果），`--csv` 导出原始观测及对应稳定边界、重测值和关联诊断，`--dump` 按需导出原始/累积谱、
归一化窗口、模型张量、候选/CNR/融合结果。`--compare` 对照已有 JSONL，记录差异而非宣称精度通过。
JSONL/CSV 及阶段目录均配有 manifest。输出拒绝覆盖已有文件；重复执行须更换输出名。
阶段导出写入指定目录下独立的 `run-UUID` 子目录，避免混入旧数据。

SCN 参数、数据格式和用户验收见 [SCN 检测开发说明](../../docs/scn_detection.md)；
FFSCN 后端参数、10 帧输入和 TensorRT 模型契约见 [FFSCN 后端方案](../../docs/ffscn_backend_plan.md)。
实现阶段不运行此工具或推理，模型兼容性和检测结果由用户实际验证。
