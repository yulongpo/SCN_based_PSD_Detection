# SCN tests

本轮只编译测试目标，不执行用例。用户运行：

```powershell
cmake --build --preset vs2026-qt611-debug --target scn_tests scn_session_tests
ctest --preset vs2026-qt611-debug
```

六组用例：`scn_algorithm`（累积/取消回滚/解码/CNR/融合/跟踪/配置）、
`scn_channel`（容量/背压/丢旧/关闭唤醒）、`scn_pipeline`（异步旧轮次失效）、
`scn_source`（DAT 元数据/时间/循环/Seek）、`scn_hash`（SHA256 已知向量）、
`scn_session`（真实 Qt 事件循环下的 FILE 背压、末帧完成、循环暂停/恢复/停止）。

测试目标使用自己的工厂函数和确定性后端，不能链接进主程序或 DetectionLab。
它不加载 TensorRT/CUDA，不需要 GPU 或 BB60C，不从网络下载测试框架。
会话集成目标使用独立 FILE 工厂，不加载 BB60C 驱动 DLL；生产程序的三类源工厂不受影响。
Debug 需要 VS 的调试 CRT。`scn_session_tests` 还依赖自动部署的 Qt Core。
文件用例只在临时目录创建自有样本并自动清理。

这些测试不替代真实模型、Qt/Direct2D 交互及硬件验收。
真实测试步骤见 `docs/scn_detection.md`；禁止用 CPU 假后端结果宣称模型精度通过。
