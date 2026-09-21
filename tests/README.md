# Tests

This migration creates the test boundary but does not add or execute tests.
The user-owned test plan should cover:

1. `algorithm/`: `DetectionEngine` interface and result contracts.
2. `source/`: source configuration, file replay and frame metadata.
3. `integration/`: `MonitoringSession` thread lifecycle and Qt snapshot flow.

Use `ctest --test-dir out/build/vs2026-qt611-debug -C Debug --output-on-failure`
after adding the test targets.
