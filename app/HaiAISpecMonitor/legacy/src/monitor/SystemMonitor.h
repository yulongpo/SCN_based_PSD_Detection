#pragma once

#include <QObject>
#include <QThread>
#include <QTimer>
#include <QVector>

// ============================================================
// MonitorWorker — 在独立 QThread 中执行系统资源采样
//
// 每 1 秒采集一次 CPU/GPU/RAM 使用率及内存信息，通过信号发给主线程。
// CPU / RAM 使用 Windows API（GetSystemTimes / GlobalMemoryStatusEx），
// GPU 使用 PDH（Performance Data Helper）查询 GPU Engine 及 GPU Memory 计数器。
// ============================================================
class MonitorWorker : public QObject
{
    Q_OBJECT

public:
    explicit MonitorWorker(QObject *parent = nullptr);
    ~MonitorWorker() override;

public slots:
    /// 在 worker 线程中启动定时采样
    void start();

signals:
    void cpuUsageUpdated(int percent);
    void gpuUsageUpdated(int percent);
    void ramUsageUpdated(int percent);

    /// CPU 运行内存（系统 RAM）总量和已用量，单位 KB
    void ramMemoryUpdated(qint64 totalKB, qint64 usedKB);

    /// GPU 专用显存总量和已用量，单位 KB
    void gpuMemoryUpdated(qint64 totalKB, qint64 usedKB);

private slots:
    void onTick();

private:
    QTimer *m_timer = nullptr;

#if defined(Q_OS_WIN)
    // CPU 差分计算用
    qint64 m_prevIdle   = 0;
    qint64 m_prevTotal  = 0;
    bool   m_hasPrev    = false;

    // GPU PDH 句柄
    void *m_gpuQuery       = nullptr;
    void *m_gpuCounter     = nullptr;   // GPU Engine 利用率计数器
    void *m_gpuMemCounter  = nullptr;   // GPU 专用显存用量计数器
    bool  m_gpuReady       = false;
    bool  m_gpuFailed      = false;

    // NVIDIA GPU 的信息
    QVector<qint64> m_nvidiaLuids;       // LUID 列表
    qint64          m_gpuTotalVramBytes = 0;  // 总专用显存（字节），来自 DXGI
#endif
};


// ============================================================
// SystemMonitor — 单例，管理 MonitorWorker 的生命周期
//
// 在主线程中调用 start() 启动后台线程，
// 其余代码通过信号连接接收数据。
// ============================================================
class SystemMonitor : public QObject
{
    Q_OBJECT

public:
    static SystemMonitor &instance();

    /// 创建工作者线程并启动采样
    void start();

    /// 停止采样并退出线程
    void stop();

signals:
    void cpuUsageUpdated(int percent);
    void gpuUsageUpdated(int percent);
    void ramUsageUpdated(int percent);
    void ramMemoryUpdated(qint64 totalKB, qint64 usedKB);
    void gpuMemoryUpdated(qint64 totalKB, qint64 usedKB);

private:
    SystemMonitor();
    ~SystemMonitor() override;
    SystemMonitor(const SystemMonitor &) = delete;
    SystemMonitor &operator=(const SystemMonitor &) = delete;

    /// 桥接：将 worker 的信号转发为自身的信号（跨线程自动排队）
    void bridgeSignals();

    QThread       *m_thread = nullptr;
    MonitorWorker *m_worker = nullptr;
};
