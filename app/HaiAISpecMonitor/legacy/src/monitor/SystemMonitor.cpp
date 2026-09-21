#include "SystemMonitor.h"

#include <QDebug>
#include <QtGlobal>
#include "BaseDef.h"

#if defined(Q_OS_WIN)
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <dxgi.h>
#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "dxgi.lib")
#endif

// ============================================================
// MonitorWorker 实现
// ============================================================

MonitorWorker::MonitorWorker(QObject *parent)
    : QObject(parent)
{
}

MonitorWorker::~MonitorWorker()
{
    if (m_timer)
        m_timer->stop();

#if defined(Q_OS_WIN)
    if (m_gpuQuery) {
        PdhCloseQuery(static_cast<PDH_HQUERY>(m_gpuQuery));
        m_gpuQuery      = nullptr;
        m_gpuCounter    = nullptr;
        m_gpuMemCounter = nullptr;
    }
#endif
}

void MonitorWorker::start()
{
    m_timer = new QTimer(this);
    m_timer->setInterval(1000);   // 每 1 秒采集一次
    connect(m_timer, &QTimer::timeout, this, &MonitorWorker::onTick);
    m_timer->start();
}

#if defined(Q_OS_WIN)

// ============================================================
// GPU 辅助函数
// ============================================================

/**
 * @brief 使用 DXGI 枚举系统中的 NVIDIA 显卡，返回其 LUID 列表
 *
 * LUID (Locally Unique Identifier) 是 Windows 中唯一标识一块显卡的 64 位值，
 * PDH GPU Engine 计数器实例名中包含 LUID，可用于过滤特定厂商的 GPU。
 */
static QVector<qint64> detectNvidiaLuids(qint64 *outTotalVramBytes = nullptr)
{
    QVector<qint64> luids;
    IDXGIFactory *factory = nullptr;

    if (CreateDXGIFactory(__uuidof(IDXGIFactory), reinterpret_cast<void **>(&factory)) != S_OK)
        return luids;

    UINT idx = 0;
    IDXGIAdapter *adapter = nullptr;
    while (factory->EnumAdapters(idx, &adapter) != DXGI_ERROR_NOT_FOUND) {
        DXGI_ADAPTER_DESC desc{};
        if (adapter->GetDesc(&desc) == S_OK) {
            const QString name = QString::fromWCharArray(desc.Description);
            if (name.contains(QStringLiteral("NVIDIA"), Qt::CaseInsensitive)) {
                // 将 LUID 高低位合并为一个 64 位值
                const qint64 luid = static_cast<qint64>(desc.AdapterLuid.LowPart)
                                  | (static_cast<qint64>(desc.AdapterLuid.HighPart) << 32);
                luids.append(luid);

                // 取第一块 NVIDIA 硬件适配器的总显存
                if (outTotalVramBytes && *outTotalVramBytes == 0) {
                    *outTotalVramBytes = static_cast<qint64>(desc.DedicatedVideoMemory);
                }

                LOG_INFO(u8"SystemMonitor: 检测到 NVIDIA GPU — %s (LUID: 0x%llX)",
                       name.toStdString().c_str(), luid);
            }
        }
        adapter->Release();
        ++idx;
    }
    factory->Release();
    return luids;
}

#endif // Q_OS_WIN

void MonitorWorker::onTick()
{
#if defined(Q_OS_WIN)
    // ============================================================
    // RAM — GlobalMemoryStatusEx（含 CPU 内存信息）
    // ============================================================
    {
        MEMORYSTATUSEX mem;
        mem.dwLength = sizeof(mem);
        if (GlobalMemoryStatusEx(&mem)) {
            emit ramUsageUpdated(static_cast<int>(mem.dwMemoryLoad));

            // CPU 运行内存：总量和已用量，单位 KB
            const qint64 totalKB = static_cast<qint64>(mem.ullTotalPhys / 1024);
            const qint64 availKB = static_cast<qint64>(mem.ullAvailPhys / 1024);
            const qint64 usedKB  = totalKB - availKB;
            emit ramMemoryUpdated(totalKB, usedKB);
        }
    }

    // ============================================================
    // CPU — GetSystemTimes 差分
    // ============================================================
    {
        FILETIME idle, kernel, user;
        if (GetSystemTimes(&idle, &kernel, &user)) {
            auto ft2int64 = [](const FILETIME &ft) -> qint64 {
                return (static_cast<qint64>(ft.dwHighDateTime) << 32)
                     + static_cast<qint64>(ft.dwLowDateTime);
            };

            const qint64 idleTime  = ft2int64(idle);
            const qint64 totalTime = ft2int64(kernel) + ft2int64(user);

            if (m_hasPrev) {
                const qint64 idleDelta  = idleTime  - m_prevIdle;
                const qint64 totalDelta = totalTime - m_prevTotal;
                if (totalDelta > 0) {
                    const int cpuPct = static_cast<int>(
                        100.0 * (1.0 - static_cast<double>(idleDelta) / totalDelta));
                    emit cpuUsageUpdated(qBound(0, cpuPct, 100));
                }
            } else {
                m_hasPrev = true;
            }
            m_prevIdle  = idleTime;
            m_prevTotal = totalTime;
        }
    }

    // ============================================================
    // GPU — PDH 查询 GPU Engine Utilization Percentage
    // ============================================================
    {
        // ---- 首次初始化 ----
        if (!m_gpuReady && !m_gpuFailed) {
            // 检测 NVIDIA GPU 的 LUID（用于构建精确的计数器路径）
            if (m_nvidiaLuids.isEmpty()) {
                m_nvidiaLuids = detectNvidiaLuids(&m_gpuTotalVramBytes);
                if (m_nvidiaLuids.isEmpty()) {
                    LOG_WARN(u8"SystemMonitor: 未检测到 NVIDIA GPU，GPU 利用率将不统计");
                }
            }

            PDH_HQUERY query = nullptr;
            if (PdhOpenQueryW(nullptr, 0, &query) == ERROR_SUCCESS) {
                PDH_HCOUNTER counter = nullptr;
                PDH_STATUS status = ERROR_FILE_NOT_FOUND;

                // 如果检测到 NVIDIA GPU，用 LUID 构建精确路径，PDH 端直接过滤
                // 实例名格式: luid_0x<HighPart>_0x<LowPart>_phys_N_eng_T_S
                if (!m_nvidiaLuids.isEmpty()) {
                    const qint64 luid = m_nvidiaLuids.first();
                    const quint32 highPart = static_cast<quint32>(luid >> 32);
                    const quint32 lowPart  = static_cast<quint32>(luid);

                    const QString nvidiaPath = QStringLiteral(
                        "\\GPU Engine(*luid_0x%1_0x%2*)\\Utilization Percentage")
                        .arg(highPart, 8, 16, QChar('0'))
                        .arg(lowPart,  8, 16, QChar('0'));

                    LOG_INFO(u8"SystemMonitor: NVIDIA GPU 计数器路径: %s",
                           nvidiaPath.toStdString().c_str());

                    status = PdhAddEnglishCounterW(
                        query,
                        reinterpret_cast<const wchar_t *>(nvidiaPath.utf16()),
                        0, &counter);

                    if (status != ERROR_SUCCESS) {
                        status = PdhAddCounterW(
                            query,
                            reinterpret_cast<const wchar_t *>(nvidiaPath.utf16()),
                            0, &counter);
                    }
                }

                // 如果 NVIDIA 路径失败或未检测到 NVIDIA，回退到通配符路径
                if (status != ERROR_SUCCESS) {
                    LOG_WARN(u8"SystemMonitor: NVIDIA 专用路径初始化失败 (0x%lX)，回退到通用路径",
                             static_cast<unsigned long>(status));

                    status = PdhAddEnglishCounterW(
                        query,
                        L"\\GPU Engine(*)\\Utilization Percentage",
                        0, &counter);

                    if (status != ERROR_SUCCESS) {
                        status = PdhAddCounterW(
                            query,
                            L"\\GPU Engine(*)\\Utilization Percentage",
                            0, &counter);
                    }
                }

                if (status == ERROR_SUCCESS) {
                    // ---- 添加 GPU 显存用量计数器 ----
                    // 计数器路径：\GPU Adapter Memory(*luid_0x<HIGH>_0x<LOW>*)\Dedicated Usage
                    PDH_HCOUNTER memCounter = nullptr;
                    if (!m_nvidiaLuids.isEmpty() && m_gpuTotalVramBytes > 0) {
                        const qint64 luid = m_nvidiaLuids.first();
                        const quint32 highPart = static_cast<quint32>(luid >> 32);
                        const quint32 lowPart  = static_cast<quint32>(luid);

                        const QString memPath = QStringLiteral(
                            "\\GPU Adapter Memory(*luid_0x%1_0x%2*)\\Dedicated Usage")
                            .arg(highPart, 8, 16, QChar('0'))
                            .arg(lowPart,  8, 16, QChar('0'));

                        PDH_STATUS memStatus = PdhAddEnglishCounterW(
                            query,
                            reinterpret_cast<const wchar_t *>(memPath.utf16()),
                            0, &memCounter);

                        if (memStatus != ERROR_SUCCESS) {
                            memStatus = PdhAddCounterW(
                                query,
                                reinterpret_cast<const wchar_t *>(memPath.utf16()),
                                0, &memCounter);
                        }

                        if (memStatus == ERROR_SUCCESS) {
                            LOG_INFO(u8"SystemMonitor: GPU 显存计数器已添加 — %s",
                                   memPath.toStdString().c_str());
                        } else {
                            LOG_WARN(u8"SystemMonitor: GPU 显存计数器添加失败 (0x%lX)",
                                     static_cast<unsigned long>(memStatus));
                        }
                    }

                    PdhCollectQueryData(query);   // 首次采样（丢弃）
                    m_gpuQuery      = static_cast<void *>(query);
                    m_gpuCounter    = static_cast<void *>(counter);
                    m_gpuMemCounter = static_cast<void *>(memCounter);
                    m_gpuReady      = true;
                } else {
                    PdhCloseQuery(query);
                    m_gpuFailed = true;
                    LOG_WARN(u8"SystemMonitor: GPU PDH 初始化失败 (0x%lX)",
                             static_cast<unsigned long>(status));
                }
            } else {
                m_gpuFailed = true;
            }
        }

        // ---- 周期性采集 ----
        static bool s_gpuFirstSample = true;
        if (m_gpuReady) {
            {
                const PDH_STATUS collectStatus = PdhCollectQueryData(
                    static_cast<PDH_HQUERY>(m_gpuQuery));
                if (collectStatus != ERROR_SUCCESS) {
                    // 首次采集失败时打印错误码，便于排查路径问题
                    static bool s_collectErrorLogged = false;
                    if (!s_collectErrorLogged) {
                        s_collectErrorLogged = true;
                        LOG_WARN(u8"SystemMonitor: PdhCollectQueryData 失败 (0x%lX)",
                                 static_cast<unsigned long>(collectStatus));
                    }
                    return;
                }
            }

            // 首个有效数据帧作为基线（百分比需要两次采样）
            if (s_gpuFirstSample) {
                s_gpuFirstSample = false;
                return;
            }

            // 通配符计数器可能返回多个 GPU Engine 实例，需用 Array API
            DWORD bufSize = 0, itemCnt = 0;
            PDH_STATUS status = PdhGetFormattedCounterArrayW(
                static_cast<PDH_HCOUNTER>(m_gpuCounter),
                PDH_FMT_DOUBLE, &bufSize, &itemCnt, nullptr);

            if (status == PDH_MORE_DATA && bufSize > 0) {
                auto *items = static_cast<PDH_FMT_COUNTERVALUE_ITEM_W *>(malloc(bufSize));
                if (items) {
                    status = PdhGetFormattedCounterArrayW(
                        static_cast<PDH_HCOUNTER>(m_gpuCounter),
                        PDH_FMT_DOUBLE, &bufSize, &itemCnt, items);

                    if (status == ERROR_SUCCESS && itemCnt > 0) {
                        // 调试：首次成功采集时打印所有实例名，便于排查格式问题
                        static bool s_instanceNamesLogged = false;
                        if (!s_instanceNamesLogged) {
                            s_instanceNamesLogged = true;
                            QStringList debugNames;
                            for (DWORD i = 0; i < itemCnt; ++i) {
                                debugNames << QString::fromWCharArray(items[i].szName);
                            }
                            LOG_INFO(u8"SystemMonitor: GPU Engine 实例 [%lu个]: %s",
                                   itemCnt, debugNames.join(" | ").toStdString().c_str());
                        }

                        // 取所有引擎的最大利用率（匹配 Task Manager 的统计方式）
                        // GPU Engine 的多个引擎（3D/copy/video_encode 等）可并行运行，
                        // 取最大值代表 GPU 整体繁忙程度，而非取无意义的平均值
                        double maxUtil = 0.0;
                        for (DWORD i = 0; i < itemCnt; ++i) {
                            if (items[i].FmtValue.doubleValue > maxUtil) {
                                maxUtil = items[i].FmtValue.doubleValue;
                            }
                        }
                        const int gpuPct = qBound(0,
                            static_cast<int>(maxUtil + 0.5), 100);
                        emit gpuUsageUpdated(gpuPct);
                    }
                    free(items);
                }
            }

            // ---- GPU 显存用量采集 ----
            if (m_gpuMemCounter && m_gpuTotalVramBytes > 0) {
                DWORD memBufSize = 0, memItemCnt = 0;
                PDH_STATUS memStatus = PdhGetFormattedCounterArrayW(
                    static_cast<PDH_HCOUNTER>(m_gpuMemCounter),
                    PDH_FMT_LARGE, &memBufSize, &memItemCnt, nullptr);

                if (memStatus == PDH_MORE_DATA && memBufSize > 0) {
                    auto *memItems = static_cast<PDH_FMT_COUNTERVALUE_ITEM_W *>(
                        malloc(memBufSize));
                    if (memItems) {
                        memStatus = PdhGetFormattedCounterArrayW(
                            static_cast<PDH_HCOUNTER>(m_gpuMemCounter),
                            PDH_FMT_LARGE, &memBufSize, &memItemCnt, memItems);

                        if (memStatus == ERROR_SUCCESS && memItemCnt > 0) {
                            // 取所有实例的最大值（单实例 = 总用量；多实例取最大避免遗漏）
                            LONGLONG maxUsageBytes = 0;
                            for (DWORD i = 0; i < memItemCnt; ++i) {
                                if (memItems[i].FmtValue.largeValue > maxUsageBytes) {
                                    maxUsageBytes = memItems[i].FmtValue.largeValue;
                                }
                            }

                            const qint64 totalKB = m_gpuTotalVramBytes / 1024;
                            const qint64 usedKB  = maxUsageBytes / 1024;
                            emit gpuMemoryUpdated(totalKB, usedKB);
                        }
                        free(memItems);
                    }
                }
            }
        }
    }
#else
    Q_UNUSED(0);
#endif
}

// ============================================================
// SystemMonitor 单例实现
// ============================================================

SystemMonitor &SystemMonitor::instance()
{
    static SystemMonitor s_instance;
    return s_instance;
}

SystemMonitor::SystemMonitor()
    : QObject(nullptr)
{
}

SystemMonitor::~SystemMonitor()
{
    stop();
}

void SystemMonitor::start()
{
    if (m_thread)
        return;   // 已在运行

    m_thread = new QThread(this);
    m_worker = new MonitorWorker();       // 无父对象（将移入工作线程）
    m_worker->moveToThread(m_thread);

    bridgeSignals();

    // 线程启动后自动调用 worker->start()
    connect(m_thread, &QThread::started, m_worker, &MonitorWorker::start);
    // 线程结束时清理 worker
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    m_thread->start();
}

void SystemMonitor::stop()
{
    if (m_thread) {
        m_thread->quit();
        m_thread->wait(3000);
        m_thread = nullptr;
        m_worker = nullptr;   // deleteLater 已处理
    }
}

void SystemMonitor::bridgeSignals()
{
    if (!m_worker)
        return;

    // 跨线程信号自动排队（Qt::AutoConnection）
    connect(m_worker, &MonitorWorker::cpuUsageUpdated,
            this,     &SystemMonitor::cpuUsageUpdated);
    connect(m_worker, &MonitorWorker::gpuUsageUpdated,
            this,     &SystemMonitor::gpuUsageUpdated);
    connect(m_worker, &MonitorWorker::ramUsageUpdated,
            this,     &SystemMonitor::ramUsageUpdated);
    connect(m_worker, &MonitorWorker::ramMemoryUpdated,
            this,     &SystemMonitor::ramMemoryUpdated);
    connect(m_worker, &MonitorWorker::gpuMemoryUpdated,
            this,     &SystemMonitor::gpuMemoryUpdated);
}
