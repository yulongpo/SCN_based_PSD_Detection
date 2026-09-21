# Legacy ISA UI and dependency snapshot

This directory is a read-only migration reference copied from `D:\project\isa`.
It preserves the original Qt UI, controller, resources, RadioAI headers and
business components so that the new UI can be migrated page by page.

The active Qt 6 application does **not** compile this directory. No active
target includes `ComFlow`, `HQSigMF`, the old component DLLs, or the legacy
controller. This boundary is intentional: the new detection hot path is based
on `MonitoringSession`, `ISpectrumSource`, `SpectrumFrame` and
`DetectionEngine`.
