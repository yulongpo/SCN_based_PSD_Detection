#include "RecordDialog.h"

#include "comm/CommonMacros.h"
#include "comm/FontManager.h"
#include "comm/ScreenScale.h"
#include "comm/ThemeManager.h"
#include "RecordProgressBar.h"
#include "content/collMonitor/tfwaterfall/HQTfwaterfall.h"
#include "content/collMonitor/spectrum/HQSpectrum.h"
#include "content/collMonitor/listbox/HQListBox.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFrame>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>

#include <limits>
#include <cmath>
#include <algorithm>

#include "BaseDef.h"

// ============================================================================
// RecordPlaybackReader — 回放文件读取线程
// ============================================================================
// 以 ~60 FPS 从频谱数据文件和对应时间戳文件中逐帧读取数据，
// 通过三缓冲 + 原子标志无锁交付给主线程消费。
//
// 频谱文件路径 : bin/data/spectrum_data/<file_name>
// 时间戳文件   : bin/data/spectrum_data/<file_name>.timestamp
// 每帧频谱大小 : len_per_spec * sizeof(float) 字节
// 每帧时间戳   : 1 × int64_t
// ============================================================================

class RecordPlaybackReader : public QThread
{
public:
    /**
     * @param specPath   频谱数据文件路径
     * @param timePath   时间戳文件路径
     * @param specSize   单帧频谱点数（len_per_spec）
     * @param slotData   冲频谱数据
     * @param slotTime   时间戳
     * @param readySlot  就绪槽位原子标志（-1=无, 0/1/2=就绪槽）
     * @param totalFrames 总帧数
     * @param isPaused   暂停标志
     * @param startFrame 起始帧索引（默认 0，从头开始；>0 则从指定帧开始）
     */
    RecordPlaybackReader(const QString &specPath, const QString &timePath,
                         int specSize,
                         float *slotData, int64_t *slotTime,
                         std::atomic<int> *readySlot,
                         int totalFrames, std::atomic<bool> *isPaused,
                         int startFrame = 0)
        : m_specPath(specPath), m_timePath(timePath)
        , m_specSize(specSize)
        , m_slotData(slotData), m_slotTime(slotTime)
        , m_readySlot(readySlot), m_totalFrames(totalFrames)
        , m_isPaused(isPaused)
        , m_startFrame(startFrame)
    {
    }

    /** @brief 请求线程安全退出 */
    void requestStop() { m_stop = true; }

protected:
    void run() override
    {
        QFile specFile(m_specPath);
        QFile timeFile(m_timePath);

        if (!specFile.open(QIODevice::ReadOnly)) {
            LOG_WARN(u8"RecordPlaybackReader: 无法打开频谱数据文件 [%s]",
                     qPrintable(m_specPath));
            return;
        }
        if (!timeFile.open(QIODevice::ReadOnly)) {
            LOG_WARN(u8"RecordPlaybackReader: 无法打开时间戳文件 [%s]",
                     qPrintable(m_timePath));
            specFile.close();
            return;
        }

        const qint64 specBytes = static_cast<qint64>(m_specSize) * sizeof(float);
        int frameCount = 0;

        // ---- 如果指定了起始帧（seek 场景），跳到对应文件位置 ----
        if (m_startFrame > 0 && m_startFrame < m_totalFrames) {
            // 频谱文件：固定帧大小，直接 seek
            qint64 specOffset = static_cast<qint64>(m_startFrame) * specBytes;
            if (!specFile.seek(specOffset)) {
                LOG_WARN(u8"RecordPlaybackReader: 频谱文件 seek 到帧 %d 失败", m_startFrame);
                specFile.close();
                timeFile.close();
                return;
            }

            // 时间戳文件：逐行跳过 m_startFrame 行（行长度可变，无法按字节 seek）
            for (int i = 0; i < m_startFrame; ++i) {
                if (timeFile.atEnd()) break;
                timeFile.readLine();
            }

            frameCount = m_startFrame;
            LOG_INFO(u8"RecordPlaybackReader: 从第 %d 帧开始读取 (共 %d 帧)",
                     m_startFrame, m_totalFrames);
        }

        while (!m_stop && frameCount < m_totalFrames) {
            // ----------------------------------------------------------------
            // 检查当前写入槽位是否被消费线程占用
            // m_readySlot == writeSlot → 该槽位数据尚未消费，等待下一周期
            // ----------------------------------------------------------------
            if (m_isPaused->load() || m_readySlot->load() != -1) {
                QThread::msleep(1);   // 让出 CPU，避免忙等
                continue;
            }

            // ---- 读取一帧频谱数据：len_per_spec 个 float ----
            if (specFile.read(reinterpret_cast<char *>(m_slotData),
                              specBytes) != specBytes) {
                break;  // 文件读取完毕或出错
            }

            // ---- 读取对应时间戳：文本格式，每行一个 int64_t ----
            const QByteArray line = timeFile.readLine().trimmed();
            if (line.isEmpty()) {
                break;  // 文件读取完毕
            }
            bool ok = false;
            *m_slotTime = line.toLongLong(&ok);
            if (!ok) {
                break;  // 格式错误
            }

            ++frameCount;

            // ---- 标记槽位就绪，主线程可消费 ----
            m_readySlot->store(0);

            // ---- ~30 FPS 节拍 ----
            QThread::msleep(33);
        }

        specFile.close();
        timeFile.close();
        LOG_INFO(u8"RecordPlaybackReader: 回放完成，共读取 %d/%d 帧",
               frameCount, m_totalFrames);
    }

private:
    QString              m_specPath;                                    ///< 频谱数据文件路径
    QString              m_timePath;                                    ///< 时间戳文件路径
    int                  m_specSize;                                    ///< 单帧频谱点数（len_per_spec）
    float               *m_slotData;                                    ///< 缓冲频谱数据指针数组
    int64_t             *m_slotTime;                                    ///< 缓冲时间戳数组
    std::atomic<int>    *m_readySlot;                                   ///< 就绪槽位原子标志
    std::atomic<bool>   *m_isPaused;                                    ///< 是否暂停读取
    int                  m_totalFrames;                                 ///< 总帧数
    int                  m_startFrame = 0;                              ///< 起始帧索引（seek 场景使用）
    std::atomic<bool>    m_stop{false};                           ///< 停止请求标志
};


RecordPlaybackReaderAll::RecordPlaybackReaderAll(std::atomic<bool>* isWriteFinish, QList<SignalDetailPerFileQueryResp::SignalItem*> *specSignals
        , QMutex *specSignalsMutex, QVector<HQSigMF::DetectionObject> *lastObjs, QVector<std::pair<HQSigMF::DetectionObject, int>> *objs)
    : m_specPath(), m_timePath()
      , m_specSize(0), m_startFrame(0), m_readTotalFrames(0)
      , m_writeAddr(nullptr), m_isWriteFinish(isWriteFinish)
      , m_specSignals(specSignals), m_specSignalsMutex(specSignalsMutex)
      , m_lastObjs(lastObjs), m_objs(objs)
{
}

void RecordPlaybackReaderAll::setWriteRange(float* writeAddr, const QString& specPath, const QString& timePath, int specSize, int startFrame, int readTotalFrames)
{
    m_specPath = specPath;
    m_timePath = timePath;
    m_specSize = specSize;
    m_startFrame = startFrame;
    m_readTotalFrames = readTotalFrames;
    m_writeAddr = writeAddr;
}

void RecordPlaybackReaderAll::requestStop()
{
    m_stop.store(true);
}

void RecordPlaybackReaderAll::run()
{
        while (!m_stop) {
            if (m_isWriteFinish->load() == false) {
                QThread::msleep(10);   // 让出 CPU，避免忙等
                continue;
            }

            QFile specFile(m_specPath);
            QFile timeFile(m_timePath);

            if (!specFile.open(QIODevice::ReadOnly)) {
                LOG_WARN(u8"RecordPlaybackReader: 无法打开频谱数据文件 [%s]",
                         qPrintable(m_specPath));
                m_isWriteFinish->store(false);
                continue;
            }
            if (!timeFile.open(QIODevice::ReadOnly)) {
                LOG_WARN(u8"RecordPlaybackReader: 无法打开时间戳文件 [%s]",
                         qPrintable(m_timePath));
                specFile.close();
                m_isWriteFinish->store(false);
                continue;
            }

            QVector<int64_t> allTimestamps;
            const qint64 specBytes = static_cast<qint64>(m_specSize) * sizeof(float);
            // ---- 如果指定了起始帧，跳到对应文件位置 ----
            if (m_startFrame > 0 && m_startFrame < m_readTotalFrames) {
                // 频谱文件：固定帧大小，直接 seek
                qint64 specOffset = static_cast<qint64>(m_startFrame) * specBytes;
                if (!specFile.seek(specOffset)) {
                    LOG_WARN(u8"RecordPlaybackReader: 频谱文件 seek 到帧 %d 失败", m_startFrame);
                    specFile.close();
                    timeFile.close();
                    continue;
                }
            }
            // 时间戳文件 全部读取
            for (int i = 0; i < m_startFrame + m_readTotalFrames; ++i) {
                if (timeFile.atEnd()) break;
                const QByteArray line = timeFile.readLine().trimmed();
                if (line.isEmpty()) {
                    break;  // 文件读取完毕
                }
                allTimestamps.append(line.toLongLong());
            }

            if (specFile.read(reinterpret_cast<char *>(m_writeAddr),
                              specBytes * m_readTotalFrames) != specBytes) {
                // break;  // 文件读取完毕或出错
            }

            // 计算OBJ
            m_objs->clear();
            HQSigMF::DetectionObject obj;
            for (int i = 0; i < allTimestamps.size(); i++)
            {
                QMutexLocker locker(m_specSignalsMutex);
                for (auto it = m_specSignals->begin(); it != m_specSignals->end();)
                {
                    if (allTimestamps[i] <= (*it)->recent_time)
                    {
                        for (int j = 0; j < (*it)->burst_num; j++)
                        {
                            if (allTimestamps[i] >= (*it)->times[j].bgn_time && allTimestamps[i] <= (*it)->times[j].end_time)
                            {
                                obj.id = (*it)->id;
                                obj.f_start_hz = (*it)->fc - (*it)->bw / 2;
                                obj.f_end_hz = (*it)->fc + (*it)->bw / 2;
                                obj.carry_type = (*it)->carry_type;
                                obj.alarm_level = (*it)->alarm_level;
                                obj.t_start_ns = (*it)->times[j].bgn_time;
                                obj.t_end_ns = (*it)->times[j].end_time;
                                if (i == allTimestamps.size() - 1) m_lastObjs->append(obj);
                                // 根据列表框的标准计算obj的出现次数
                                bool find = false;
                                for (int k = 0; k < m_objs->size(); k++)
                                {
                                    if ((*m_objs)[k].first.id == obj.id && (*m_objs)[k].first.t_start_ns != obj.t_start_ns)
                                    {
                                        (*m_objs)[k].second++;
                                        (*m_objs)[k].first.t_start_ns = obj.t_start_ns;
                                        (*m_objs)[k].first.t_end_ns = obj.t_end_ns;
                                        (*m_objs)[k].first.carry_type = 1;
                                        find = true;
                                        break;
                                    }
                                    else if ((*m_objs)[k].first.id == obj.id)
                                    {
                                        (*m_objs)[k].first.t_end_ns = obj.t_end_ns;
                                        find = true;
                                        break;
                                    }
                                }
                                if (find == false)
                                {
                                    m_objs->append(std::make_pair(obj, 1));
                                }
                                break;
                            }
                        }
                        ++it;
                    }
                    else
                    {
                        it = m_specSignals->erase(it);
                    }
                }
            }

            // ---- 标记槽位就绪，主线程可消费 ----
            m_isWriteFinish->store(false);
            specFile.close();
            timeFile.close();
        }
}

RecordDialog::RecordDialog(const SpectrumFileInfo &data, const QString &signalPath, QWidget *parent)
    : QDialog(parent)
    , m_signalPath(signalPath)
{
    setObjectName("RecordDialog");
    setWindowFlags(Qt::FramelessWindowHint | Qt::Dialog);
    setAttribute(Qt::WA_TranslucentBackground);
    setModal(true);
    setFocusPolicy(Qt::StrongFocus);

    m_signalDetails.file_id    = 0;
    m_signalDetails.signal_num = 0;
    m_signalDetails.item       = nullptr;

    // 设置为屏幕可用区域的 90%
    if (QScreen *sc = QApplication::primaryScreen()) {
        QSize avail = sc->availableGeometry().size();
        setFixedSize(avail.width() * 0.9, avail.height() * 0.9);
    } else {
        const int w = scaledPx(1200, 800);
        const int h = scaledPx(900, 600);
        setFixedSize(w, h);
    }

    int mainMargin = scaledPx(12, 0);
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(mainMargin, mainMargin, mainMargin, mainMargin);
    m_mainLayout->setSpacing(8);

    setupTitleBar();
    setupContent(data);

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &RecordDialog::applyTheme);
    applyTheme();

    // 始终在屏幕中央弹出（parent->geometry() 在子控件中返回的是相对坐标，不可靠）
    if (QScreen *sc = QApplication::primaryScreen())
        move(sc->availableGeometry().center() - rect().center());

    qApp->installEventFilter(this);

    m_playbackAllThread = new RecordPlaybackReaderAll(&m_isWriteFinish, &m_specSignals, &m_specSignalsMutex, &m_lastObjs, &m_objs);
    m_playbackAllThread->start();
    // 启动回放读取
    startPlayback();
    m_loadingWidget = new LoadingWidget(this);
    m_loadingWidget->hide();
}

void RecordDialog::dialogSignalDetail(const SignalDetailPerFileQueryResp& resp)
{
    if (m_signalDetails.item != nullptr && m_signalDetails.signal_num > 0) {
        for (int32_t i = 0; i < m_signalDetails.signal_num; ++i) {
            delete[] m_signalDetails.item[i].times;
        }
        delete[] m_signalDetails.item;
    }
    QMutexLocker lock(&m_specSignalsMutex);
    m_specSignals.clear();
    m_signalDetails.file_id    = resp.file_id;
    m_signalDetails.signal_num = resp.signal_num;
    m_signalDetails.item       = nullptr;

    if (resp.signal_num > 0) {
        m_signalDetails.item = new SignalDetailPerFileQueryResp::SignalItem[resp.signal_num];
        for (int32_t i = 0; i < resp.signal_num; ++i) {
            const auto& src = resp.item[i];
            auto&       dst = m_signalDetails.item[i];
            m_specSignals.append(m_signalDetails.item + i);

            // 拷贝基本类型成员
            dst.id           = src.id;
            dst.fc           = src.fc;
            dst.bw           = src.bw;
            dst.carry_type   = src.carry_type;
            dst.alarm_level = src.alarm_level;
            dst.recent_time  = src.recent_time;
            dst.burst_num    = src.burst_num;
            dst.avg_duration = src.avg_duration;

            // 深拷贝 times 数组（长度由 burst_num 决定）
            dst.times = nullptr;
            if (src.burst_num > 0) {
                dst.times = new SignalDetailPerFileQueryResp::TimeRange[src.burst_num];
                for (int32_t j = 0; j < src.burst_num; ++j) {
                    dst.times[j].bgn_time = src.times[j].bgn_time;
                    dst.times[j].end_time = src.times[j].end_time;
                }
            }
        }
    }
}

void RecordDialog::addSignalNum(int type, int num)
{
    switch (type)
    {
    case 0:
        m_totalCountLabel->setText(QString::number(m_totalCountLabel->text().toInt() + num));
        break;
    case 1:
        m_generalAlertLabel->setText(QString::number(m_generalAlertLabel->text().toInt() + num));
        break;
    case 2:
        m_severeAlertLabel->setText(QString::number(m_severeAlertLabel->text().toInt() + num));
        break;
    default:
        break;
    }
}

void RecordDialog::slotPause(bool paused)
{
    if (paused) {
        // 暂停：设置原子标志，读取线程检测到此标志后会休眠等待
        m_isPaused.store(true);
    } else {
        // 恢复播放：清除暂停标志
        m_isPaused.store(false);
        // 如果读取线程不存在或已结束（seek 预览后 / 自然播放完毕），
        // 从当前预览帧的下一帧开始启动实时回放，避免重复渲染最后一帧预览
        if (!m_playbackThread || m_playbackThread->isFinished()) {
            if (m_currentPlayFrame + 1 != m_fileInfo.spec_num)
            {
                const int startFrame = qMin(m_currentPlayFrame + 1, m_fileInfo.spec_num - 1);
                startPlayback(startFrame);
            }
        }
    }
}

// ============================================================================
// 回放控制
// ============================================================================

RecordDialog::~RecordDialog()
{
    if (m_playbackAllThread) {
        auto *reader = static_cast<RecordPlaybackReaderAll *>(m_playbackAllThread);
        reader->requestStop();
        if (!reader->wait(3000)) {
            LOG_WARN(u8"RecordDialog::stopPlayback: 等待线程超时，强制终止");
            reader->terminate();
            reader->wait();
        }
        delete m_playbackAllThread;
        m_playbackAllThread = nullptr;
    }
    stopPlayback();
    m_specSignals.clear();
    if (m_signalDetails.item != nullptr && m_signalDetails.signal_num > 0) {
        for (int32_t i = 0; i < m_signalDetails.signal_num; ++i) {
            delete[] m_signalDetails.item[i].times;
        }
        delete[] m_signalDetails.item;
    }
}

void RecordDialog::closeEvent(QCloseEvent *event)
{
    stopPlayback();
    QDialog::closeEvent(event);
}

void RecordDialog::startPlayback(int startFrame)
{
    // ---- 清理之前的播放状态 ----
    stopPlayback();

    // ---- 构建文件路径 ----
    // 优先使用后端下发的频谱数据存储路径；为空时回退到默认相对路径
    QString baseDir = m_signalPath.isEmpty()
                          ? QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../data/spectrum_data/"))
                          : m_signalPath;
    // 兼容路径末尾不带分隔符的情况
    if (!baseDir.endsWith(QLatin1Char('/')) && !baseDir.endsWith(QLatin1Char('\\')))
        baseDir += QLatin1Char('/');
    const QString specPath = baseDir + QString::fromUtf8(m_fileInfo.file_name);
    const QString timePath = specPath.mid(0, specPath.lastIndexOf(".")) + QStringLiteral(".timestamp");

    if (m_fileInfo.len_per_spec <= 0 || m_fileInfo.spec_num <= 0) {
        LOG_WARN(u8"RecordDialog::startPlayback: 参数无效 "
                 "(len_per_spec=%d, spec_num=%d)",
                 m_fileInfo.len_per_spec, m_fileInfo.spec_num);
        return;
    }

    double bgnFreq = m_fileInfo.fc - m_fileInfo.span / 2;
    double endFreq = m_fileInfo.fc + m_fileInfo.span / 2;
    int64_t timeSpan = (m_fileInfo.end_time - m_fileInfo.bgn_time) / 1e9;

    // ---- 首次播放时预加载全部时间戳，供后续 seek 二分查找使用 ----
    if (m_allTimestamps.size() != m_fileInfo.spec_num) {
        m_allTimestamps.clear();
        m_allTimestamps.reserve(m_fileInfo.spec_num);
        QFile timeFile(timePath);
        if (timeFile.open(QIODevice::ReadOnly)) {
            while (!timeFile.atEnd()) {
                const QByteArray line = timeFile.readLine().trimmed();
                if (line.isEmpty()) break;
                bool ok = false;
                int64_t ts = line.toLongLong(&ok);
                if (!ok) break;
                m_allTimestamps.append(ts);
            }
            timeFile.close();
            LOG_INFO(u8"RecordDialog: 预加载 %d 个时间戳用于 seek 定位",
                     static_cast<int>(m_allTimestamps.size()));
        }
    }

    // 注意：图表清除由 seekToPosition() 在加载预览帧前完成；
    // 此处不再清除，避免抹掉已渲染的预览帧。

    // 分辨率/范围只在首次配置，后续重启播放不再调用 setResolution，
    // 防止内部缓冲区重分配清空已加载的预览数据
    if (!m_rangeConfigured) {
        m_waterfall->setXRangeLimit(bgnFreq, endFreq);
        m_waterfall->setYRangeLimit(-25, 0);
        m_waterfall->setResolution(m_fileInfo.len_per_spec, 100,
                                    QCPRange(bgnFreq, endFreq),
                                    QCPRange(-25, 0));
        m_spectrum->setXRangeLimit(bgnFreq, endFreq);
        m_spectrum->setResolution(m_fileInfo.len_per_spec,
                                   QCPRange(bgnFreq, endFreq));
        // 限制频谱图Y轴范围
        m_spectrum->setYRangeLimit(m_fileInfo.ref_level - 100.0f, m_fileInfo.ref_level);
        m_spectrum->setYRange(m_fileInfo.ref_level - 100.0f, m_fileInfo.ref_level);
        m_rangeConfigured = true;
    }

    // ---- 更新进度条总帧数、总时长（显示用）和起始帧位置 ----
    m_progressBar->setTotalFrames(m_fileInfo.spec_num);
    m_progressBar->setTotalTime(timeSpan);
    m_progressBar->setPosition(startFrame);

    // ---- 分配缓冲 ----
    m_slotData = new float[static_cast<size_t>(m_fileInfo.len_per_spec)]();
    m_slotTime = 0;
    m_readySlot.store(-1);

    // ---- 启动文件读取线程（传入 startFrame 支持 seek） ----
    m_playbackThread = new RecordPlaybackReader(
        specPath, timePath, m_fileInfo.len_per_spec,
        m_slotData, &m_slotTime, &m_readySlot,
        m_fileInfo.spec_num, &m_isPaused, startFrame);
    m_playbackThread->start();

    // ---- 启动主线程消费定时器（60 FPS） ----
    if (!m_playbackConsumeTimer) {
        m_playbackConsumeTimer = new QTimer(this);
        m_playbackConsumeTimer->setObjectName(
            QStringLiteral("PlaybackConsumeTimer"));
        connect(m_playbackConsumeTimer, &QTimer::timeout, this, [this]() {
            // seek 进行中时跳过旧数据消费，避免显示错乱
            if (m_isSeeking.load())
            {
                return;
            }

            bool flag = false;// 用以判断是否还有最后一帧
            // 若读取线程已结束（文件读完/出错），停止定时器
            if (m_playbackThread && m_playbackThread->isFinished())
            {
                if (m_readySlot.load() == 0)
                {
                    flag = true;
                }
                else
                {
                    m_playbackConsumeTimer->stop();
                    return;
                }
            }

            // 检查是否有就绪槽位
            const int slot = m_readySlot.load();
            if (slot != 0)
                return;

            // 计算色阶
            if (m_firstFrame)
            {
                m_firstFrame = false;
                double min = 10000.0, max = -10000.0;
                getMinMax(m_slotData, 0, m_fileInfo.len_per_spec, min, max);
                m_waterfall->setColorDataRange(min, max);
            }

            // ---- 将数据发布到时频瀑布图和频谱图 ----
            m_waterfall->post(m_slotTime / 1e6, m_slotData);
            m_spectrum->post(m_slotData);
            // 添加标记
            m_spectrum->clearMarks();
            HQSigMF::DetectionObject obj;
            QMutexLocker lock(&m_specSignalsMutex);
            for (auto it = m_specSignals.begin(); it != m_specSignals.end(); )
            {
                if (m_slotTime <= (*it)->recent_time)
                {
                    for (int i = 0; i < (*it)->burst_num; i++)
                    {
                        if (m_slotTime >= (*it)->times[i].bgn_time && m_slotTime <= (*it)->times[i].end_time)
                        {
                            obj.id = (*it)->id;
                            obj.f_start_hz = (*it)->fc - (*it)->bw / 2;
                            obj.f_end_hz = (*it)->fc + (*it)->bw / 2;
                            obj.carry_type = (*it)->carry_type;
                            obj.alarm_level = (*it)->alarm_level;
                            obj.t_start_ns = (*it)->times[i].bgn_time;
                            obj.t_end_ns = (*it)->times[i].end_time;
                            m_spectrum->addObj(&obj);
                            m_listBox->addRow(&obj);
                            m_waterfall->addObj(&obj);
                            break;
                        }
                    }
                    ++it;
                }
                else
                {
                    it = m_specSignals.erase(it);
                }
            }
            lock.unlock();
            // ---- 更新当前播放帧索引（用于播放完毕后恢复定位，同时更新进度条） ----
            auto tsIt = std::lower_bound(m_allTimestamps.begin(), m_allTimestamps.end(), m_slotTime);
            if (tsIt != m_allTimestamps.end() && *tsIt == m_slotTime) {
                m_currentPlayFrame = static_cast<int>(tsIt - m_allTimestamps.begin());
                m_progressBar->setPosition(m_currentPlayFrame + 1);
            }

            // ---- 释放槽位，允许写入线程继续填充 ----
            m_readySlot.store(-1);

            if (flag)
            {
                m_playbackConsumeTimer->stop();
                return;
            }
        });
    }
    m_playbackConsumeTimer->start(33);  // ~30 FPS
}

void RecordDialog::stopPlayback()
{
    // ---- 停止消费定时器 ----
    if (m_playbackConsumeTimer)
        m_playbackConsumeTimer->stop();

    // ---- 停止并销毁读取线程 ----
    if (m_playbackThread) {
        auto *reader = static_cast<RecordPlaybackReader *>(m_playbackThread);
        reader->requestStop();
        if (!reader->wait(3000)) {
            LOG_WARN(u8"RecordDialog::stopPlayback: 等待线程超时，强制终止");
            reader->terminate();
            reader->wait();
        }
        delete m_playbackThread;
        m_playbackThread = nullptr;
    }

    // ---- 释放缓冲 ----
    if (m_slotData != nullptr) delete[] m_slotData;
    m_slotData = nullptr;
    m_slotTime = 0;
    m_readySlot.store(-1);
}

// ============================================================================
// seekToPosition — 拖拽进度条时加载目标帧+前100帧预览，不启动实时回放
// ============================================================================

void RecordDialog::seekToPosition(int targetFrame)
{
    // 无时间戳数据或文件信息无效时无法 seek
    if (m_allTimestamps.isEmpty() || m_fileInfo.spec_num <= 0) {
        LOG_WARN(u8"RecordDialog::seekToPosition: 无时间戳数据，无法 seek");
        return;
    }

    // 边界保护：帧索引直接 clamp 到有效范围
    const int bestFrame = qBound(0, targetFrame, m_fileInfo.spec_num - 1);

    LOG_INFO(u8"RecordDialog::seekToPosition: 目标帧=%d / 总帧=%d (ts=%lld)",
             bestFrame, m_fileInfo.spec_num,
             m_allTimestamps.value(bestFrame, 0));
    m_timeIndex = bestFrame;
    // ---- 停止当前回放（线程 + 定时器） ----
    stopPlayback();

    // ---- 计算预览范围：目标帧往前100帧，最少到第0帧 ----
    const int previewStartFrame = qMax(0, bestFrame - 99);
    const int previewCount      = bestFrame - previewStartFrame + 1;  // 预览帧数

    // ---- 打开文件准备读取 ----
    QString baseDir = m_signalPath.isEmpty()
                          ? QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../data/spectrum_data/"))
                          : m_signalPath;
    // 兼容路径末尾不带分隔符的情况
    if (!baseDir.endsWith(QLatin1Char('/')) && !baseDir.endsWith(QLatin1Char('\\')))
        baseDir += QLatin1Char('/');
    const QString specPath = baseDir + QString::fromUtf8(m_fileInfo.file_name);
    const QString timePath = specPath.mid(0, specPath.lastIndexOf(".")) + QStringLiteral(".timestamp");

    // ---- 标记 seek 状态并清除旧图表数据  m_playbackConsumeTimer中处理后续 ----
    if (!m_playbackConsumeAllTimer)
    {
        m_playbackConsumeAllTimer = new QTimer(this);
        connect(m_playbackConsumeAllTimer, &QTimer::timeout, this, [this]()
        {
            if (m_isWriteFinish.load() == false)
            {
                for (int i = 0; i < m_lastObjs.size(); i++)
                {
                    m_spectrum->addObj(&m_lastObjs[i]);
                }
                for (int i = 0; i < m_objs.size(); i++)
                {
                    m_listBox->addRow(&m_objs[i].first, m_objs[i].second);
                    m_waterfall->addObj(&m_objs[i].first);
                }
                std::vector<int64_t>& timeBuf = m_waterfall->timeBuf();
                const int previewStartFrame = qMax(0, m_timeIndex - 99);// -99 是100帧
                const int previewCount      = m_timeIndex - previewStartFrame;  // 预览帧数
                for (int i = 0; i <= previewCount; i++)
                {
                    timeBuf[i] = m_allTimestamps[i + previewStartFrame] / 1e6;
                }
                m_waterfall->updateTimesAndMax(previewCount + 1);
                m_spectrum->post(m_waterfall->tfData() + previewCount * m_fileInfo.len_per_spec);
                // 恢复 UI 交互：启用图表更新 + 启用控件操作
                m_waterfall->setUpdatesEnabled(true);
                m_spectrum->setUpdatesEnabled(true);
                m_listBox->setUpdatesEnabled(true);
                m_progressBar->setEnabled(true);   // 恢复进度条拖拽
                m_waterfall->setEnabled(true);     // 恢复瀑布图交互
                m_spectrum->setEnabled(true);      // 恢复频谱图交互
                m_listBox->setEnabled(true);       // 恢复列表交互
                m_closeBtn->setEnabled(true);      // 恢复关闭按钮

                m_isSeeking.store(false);
                m_playbackConsumeAllTimer->stop();
                m_loadingWidget->stopLoading();
            }
        });
    }
    m_isSeeking.store(true);
    m_loadingWidget->startLoading();

    // 加载期间禁止 UI 交互：禁用图表更新 + 禁用控件操作
    m_waterfall->setUpdatesEnabled(false);
    m_spectrum->setUpdatesEnabled(false);
    m_listBox->setUpdatesEnabled(false);
    m_progressBar->setEnabled(false);   // 禁止拖拽进度条
    m_waterfall->setEnabled(false);     // 禁止瀑布图交互（缩放/拖拽）
    m_spectrum->setEnabled(false);      // 禁止频谱图交互
    m_listBox->setEnabled(false);       // 禁止列表点击/滚动
    m_closeBtn->setEnabled(false);      // 禁止关闭弹窗

    m_waterfall->clearData();
    m_waterfall->clearMarks();
    m_spectrum->clearData();
    m_spectrum->clearMarks();
    m_listBox->clear();

    // 重新填充信号列表（全部信号重新扫描，因为回退了时间轴）
    QMutexLocker locker(&m_specSignalsMutex);
    m_specSignals.clear();
    if (m_signalDetails.item != nullptr && m_signalDetails.signal_num > 0) {
        for (int32_t i = 0; i < m_signalDetails.signal_num; ++i) {
            m_specSignals.append(m_signalDetails.item + i);
        }
    }
    locker.unlock();
    m_totalCountLabel->setText("0");
    m_generalAlertLabel->setText("0");
    m_severeAlertLabel->setText("0");
    m_lastObjs.clear();
    auto *reader = static_cast<RecordPlaybackReaderAll *>(m_playbackAllThread);
    reader->setWriteRange(m_waterfall->tfData(), specPath, timePath, m_fileInfo.len_per_spec, previewStartFrame, previewCount);
    m_isWriteFinish.store(true);
    m_playbackConsumeAllTimer->start(10);

    // ---- 记录当前帧索引，供后续点击播放时从下一帧开始实时回放 ----
    m_currentPlayFrame = bestFrame;
}

void RecordDialog::getMinMax(float* data, int startIndex, int endIndex, double& min, double& max)
{
    // 首先，找到10个最小值（如果数据少于10个，则取所有值）
    int dataSize = endIndex - startIndex;
    if (dataSize <= 0) {
        min = 0.0;
        max = 80.0; // 如果没有数据，使用默认值
        return;
    }

    // 确定要考虑的值数量（10个或可用数据大小中的较小值）
    int numValuesToConsider = std::min(10, dataSize);

    // 创建向量来存储最小值
    std::vector<float> smallestValues;
    smallestValues.reserve(numValuesToConsider);

    // 用前numValuesToConsider个值初始化
    for (int i = startIndex; i < startIndex + numValuesToConsider; i++) {
        smallestValues.push_back(data[i]);
    }

    // 按升序排序初始值，使最大值在末尾
    std::sort(smallestValues.begin(), smallestValues.end());

    // 处理剩余的值
    for (int i = startIndex + numValuesToConsider; i < endIndex; i++) {
        float currentValue = data[i];

        // 如果当前值小于我们最小值列表中的最大值
        if (currentValue < smallestValues.back()) {
            // 用当前值替换最大值
            smallestValues.back() = currentValue;

            // 重新排序以保持升序
            std::sort(smallestValues.begin(), smallestValues.end());
        }
    }

    // 计算最小值的平均值
    double sum = 0.0;
    for (float val : smallestValues) {
        sum += val;
    }
    double average = sum / smallestValues.size();

    // 根据要求设置最小值和最大值
    min = average;
    max = min + 60.0;
}

void RecordDialog::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // 背景填充（整体背景色固定 14,14,29）
    QPainterPath bgPath;
    bgPath.addRoundedRect(QRectF(rect()), 12, 12);
    p.fillPath(bgPath, QColor(14, 14, 29));

    // 边框：内缩 0.5px 避免半透明像素被裁剪
    const QColor bd = ThemeManager::instance().color("dialog.borderColor");
    if (bd.isValid()) {
        QPainterPath borderPath;
        borderPath.addRoundedRect(
            QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5),
            12 - 0.5, 12 - 0.5);
        QPen pen(bd, scaledPx(1, 1));
        pen.setCosmetic(true);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawPath(borderPath);
    }
}

// ============================================================================
// 标题栏
// ============================================================================

void RecordDialog::setupTitleBar()
{
    const int titleH = scaledPx(44, 30);
    const int btnSz  = scaledPx(20, 14);

    m_titleBar = new QWidget(this);
    m_titleBar->setObjectName("RecordDialogTitleBar");
    m_titleBar->setFixedHeight(titleH);

    auto *lay = new QHBoxLayout(m_titleBar);
    lay->setContentsMargins(0, 0, scaledPx(8, 4), 0);
    lay->setSpacing(0);

    // 蓝色竖线
    m_accentLine = new QLabel(m_titleBar);
    m_accentLine->setObjectName("RecordDialogAccentLine");
    m_accentLine->setFixedSize(scaledPx(4, 2), scaledPx(18, 12));
    m_accentLine->setStyleSheet(
        "QLabel { background-color: rgba(10,140,254,255); border-radius: 5px; border: none; }");
    lay->addWidget(m_accentLine);
    lay->addSpacing(scaledPx(10, 5));

    // 标题
    m_titleLabel = new QLabel(QStringLiteral("回放"), m_titleBar);
    m_titleLabel->setObjectName("RecordDialogTitle");
    QFont tf = FontManager::instance().font(scaledPx(15, 10), QFont::Bold);
    m_titleLabel->setFont(tf);
    lay->addWidget(m_titleLabel);
    lay->addStretch();

    // 关闭按钮
    m_closeBtn = new QPushButton(m_titleBar);
    m_closeBtn->setObjectName("RecordDialogCloseBtn");
    m_closeBtn->setFixedSize(btnSz, btnSz);
    m_closeBtn->setFlat(true);
    m_closeBtn->setCursor(Qt::PointingHandCursor);

    QPixmap cp(":/recordplayback/close.png");
    if (!cp.isNull()) {
        cp.setDevicePixelRatio(ScreenScale::instance().dpr());
        m_closeBtn->setIcon(QIcon(cp));
        m_closeBtn->setIconSize(QSize(btnSz, btnSz));
    } else {
        m_closeBtn->setText("X");
    }
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::close);
    lay->addWidget(m_closeBtn);

    m_mainLayout->addWidget(m_titleBar);
}

// ============================================================================
// 内容区 — 顶部信息面板（圆角矩形）+ 底部图表面板（圆角矩形）
// ============================================================================

void RecordDialog::setupContent(const SpectrumFileInfo& data)
{
    m_fileInfo = data;
    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();
    const int sideMar  = scaledPx(16, 10);   // 面板统一水平边距

    // ================================================================
    // 1. 信息面板（圆角矩形，含左 QGridLayout + 右侧统计面板）
    // ================================================================
    {
        m_infoPanel = new QWidget(this);
        m_infoPanel->setObjectName("RecordDialogInfoPanel");
        m_infoPanel->setAttribute(Qt::WA_StyledBackground, true);

        auto *outerLayout = new QHBoxLayout(m_infoPanel);
        const int tbMar   = scaledPx(6, 4);
        outerLayout->setContentsMargins(sideMar, tbMar, sideMar, tbMar);
        outerLayout->setSpacing(0);

        // ---- 左侧：两行信息（QGridLayout 水平和垂直对齐） ----
        {
            m_contentWidget = new QWidget(m_infoPanel);
            m_contentWidget->setObjectName("RecordDialogContent");

            auto *grid = new QGridLayout(m_contentWidget);
            grid->setContentsMargins(0, 0, 0, 0);
            grid->setHorizontalSpacing(scaledPx(8, 4));
            grid->setVerticalSpacing(scaledPx(12, 2));

            QFont nameFont = FontManager::instance().font(scaledPx(12, 9), QFont::Bold);
            QFont valFont  = FontManager::instance().font(scaledPx(12, 9), QFont::Bold);

            auto addPair = [&](int row, int col,
                               const QString &name, const QString &value,
                               QLabel *&valOut) {
                auto *nl = new QLabel(name, m_contentWidget);
                nl->setFont(nameFont);
                nl->setStyleSheet("color: rgba(255,255,255,0.5); background: transparent;");
                nl->setObjectName("RecordDialogInfoLabel");
                grid->addWidget(nl, row, col * 2);

                valOut = new QLabel(value, m_contentWidget);
                valOut->setFont(valFont);
                valOut->setStyleSheet("color: rgb(255,255,255); background: transparent;");
                valOut->setObjectName("RecordDialogInfoValue");
                grid->addWidget(valOut, row, col * 2 + 1);
            };

            // 单行布局（col 0~8）
            addPair(0, 0, QStringLiteral("文件："),     data.file_name,     m_filenameVal);
            addPair(0, 1, QStringLiteral("中心频率："), QString::number(data.fc / 1e6, 'f', 6) + "MHz",     m_centerFreqVal);
            addPair(0, 2, QStringLiteral("扫宽："),     QString::number(data.span / 1e6, 'f', 6) + "MHz",      m_bandwidthVal);
            addPair(0, 3, QStringLiteral("采集设备："), data.source_name,     m_deviceNameVal);
            addPair(0, 4, QStringLiteral("RBW："),      QString::number(data.rbw / 1e3, 'f', 3) + "kHz",            m_rbwVal);
            addPair(1, 0, QStringLiteral("参考电平："), QString::number(data.ref_level) + "dBm",       m_refLevelVal);
            addPair(1, 1, QStringLiteral("频谱点数："), QString::number(data.len_per_spec),      m_lenPerSpecVal);
            addPair(1, 2, QStringLiteral("开始时间："), epochToTimeStr(data.bgn_time / 1e6),      m_startTimeVal);
            addPair(1, 3, QStringLiteral("截止时间："), epochToTimeStr(data.end_time / 1e6),        m_endTimeVal);
            addPair(1, 4, QStringLiteral("录制状态："), u8"已录制",   m_recordStatusVal);

            // 值列均匀拉伸，名称列保持紧凑 → 水平和垂直对齐
            for (int c = 0; c < 5; ++c)
                grid->setColumnStretch(c * 2 + 1, 1);

            outerLayout->addWidget(m_contentWidget, 0, Qt::AlignVCenter);
        }

        // ---- 右侧：三个统计项（与 CollMonitorMenu 样式一致） ----
        {
            auto *statsWidget = new QWidget(m_infoPanel);
            auto *statsHLay  = new QHBoxLayout(statsWidget);
            statsHLay->setContentsMargins(scaledPx(16, 8), 0, 0, 0);
            statsHLay->setSpacing(scaledPx(16, 8));

            // 构建一个统计项（图标 + 标签 + 数字）
            auto createItem = [&](const QString &iconPath,
                                  const QString &label,
                                  const QString &colorKey,
                                  const QString &countText,
                                  QLabel *&numOut) -> QWidget * {
                const int iconBgSize = static_cast<int>(DPR_REAL(40.0 * scale, dpr));
                const int iconSize   = static_cast<int>(DPR_REAL(18.0 * scale, dpr));
                const int iconRadius = static_cast<int>(DPR_REAL(12.0 * scale, dpr));
                const QString numCol = ThemeManager::instance().colorString(colorKey);

                auto *item = new QWidget(statsWidget);
                auto *hLay = new QHBoxLayout(item);
                hLay->setContentsMargins(0, 0, 0, 0);
                hLay->setSpacing(static_cast<int>(DPR_REAL(6.0 * scale, dpr)));

                // 圆角图标背景
                auto *iconBg = new QWidget(item);
                iconBg->setFixedSize(iconBgSize, iconBgSize);
                item->setFixedHeight(iconBgSize);
                iconBg->setObjectName(QStringLiteral("StatIcon_%1").arg(colorKey));  // 供 applyTheme 查找
                iconBg->setStyleSheet(QString(
                    "background-color: %1; border-radius: %2px;")
                    .arg(numCol).arg(iconRadius));
                auto *bgLay = new QVBoxLayout(iconBg);
                bgLay->setContentsMargins(0, 0, 0, 0);
                bgLay->setAlignment(Qt::AlignCenter);

                auto *iconLbl = new QLabel(iconBg);
                QPixmap pix(iconPath);
                if (!pix.isNull()) {
                    pix.setDevicePixelRatio(dpr);
                    iconLbl->setPixmap(pix);
                }
                iconLbl->setScaledContents(true);
                iconLbl->setFixedSize(iconSize, iconSize);
                iconLbl->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
                bgLay->addWidget(iconLbl);
                hLay->addWidget(iconBg);

                // 右侧文字（上标签 + 下数字）
                auto *txtW = new QWidget(item);
                auto *vLay = new QVBoxLayout(txtW);
                vLay->setContentsMargins(0, 0, 0, 0);
                vLay->setSpacing(0);

                auto *topLbl = new QLabel(label, txtW);
                topLbl->setStyleSheet(QString("color: %1; background: transparent; border: none;")
                    .arg(ThemeManager::instance().colorString("titleBar.collectMonitorColor")));
                topLbl->setFont(FontManager::instance().font(
                    static_cast<int>(DPR_REAL(12.0 * scale, dpr))));
                topLbl->setFixedHeight(static_cast<int>(DPR_REAL(18.0 * scale, dpr)));
                vLay->addWidget(topLbl);

                numOut = new QLabel(countText, txtW);
                numOut->setStyleSheet(QString(
                    "color: %1; background: transparent; border: none; font-weight: bold;")
                    .arg(numCol));
                numOut->setFont(FontManager::instance().font(
                    static_cast<int>(DPR_REAL(22.0 * scale, dpr)), QFont::Bold));
                vLay->addWidget(numOut);

                hLay->addWidget(txtW);
                return item;
            };

            statsHLay->addWidget(createItem(
                QStringLiteral(":/collect/critical_alert.png"),
                QStringLiteral("严重警告"),
                QStringLiteral("collStatPanel.criticalAlertColor"),
                QStringLiteral("0"), m_severeAlertLabel));
            statsHLay->addWidget(createItem(
                QStringLiteral(":/collect/general_alarm.png"),
                QStringLiteral("一般警告"),
                QStringLiteral("collStatPanel.generalAlarmColor"),
                QStringLiteral("0"), m_generalAlertLabel));
            statsHLay->addWidget(createItem(
                QStringLiteral(":/collect/signal_total.png"),
                QStringLiteral("信号总数"),
                QStringLiteral("collStatPanel.signalTotalColor"),
                QStringLiteral("0"), m_totalCountLabel));

            outerLayout->addWidget(statsWidget, 0, Qt::AlignVCenter);
        }

        m_mainLayout->addWidget(m_infoPanel);
    }

    // ================================================================
    // 2. 图表面板（圆角矩形，含瀑布图 + 频谱图 + 信号列表框）
    // ================================================================
    {
        m_graphPanel = new QWidget(this);
        m_graphPanel->setObjectName("RecordDialogGraphPanel");
        m_graphPanel->setAttribute(Qt::WA_StyledBackground, true);

        auto *graphLayout = new QVBoxLayout(m_graphPanel);
        graphLayout->setContentsMargins(0, 0, 0, 0);
        graphLayout->setSpacing(0);

        // 分隔线辅助
        auto createSep = [&]() -> QFrame * {
            auto *sep = new QFrame(m_graphPanel);
            sep->setFrameShape(QFrame::HLine);
            sep->setFrameShadow(QFrame::Plain);
            sep->setFixedHeight(1);
            sep->setStyleSheet(
                QStringLiteral("QFrame { border: none; background-color: %1; }")
                    .arg(ThemeManager::instance().colorString("CollMonitor.borderColor")));
            return sep;
        };

        // ---- 时频瀑布图 ----
        m_waterfall = new HQTfwaterfall(m_graphPanel);
        m_waterfall->setResolution(1024, 100,
                                   QCPRange(0, 1000), QCPRange(0, 10));
        m_waterfall->setColorDataRange(-100.0, 0.0);
        graphLayout->addWidget(m_waterfall, 1);
        m_waterfall->setOpenGl(true);

        // ---- 分隔线1 ----
        m_separator1 = createSep();
        graphLayout->addWidget(m_separator1);

        // ---- 频谱图 ----
        m_spectrum = new HQSpectrum(m_graphPanel);
        m_spectrum->setResolution(1024, QCPRange(0, 1000));
        m_spectrum->hideMaxAndAvgBtn();
        graphLayout->addWidget(m_spectrum, 2);

        // ---- X 轴范围双向同步 ----
        using RangeSig = void(QCPAxis::*)(const QCPRange &);
        // 频谱图 x 轴变化 → 同步到时频图
        connect(m_spectrum->xAxis, static_cast<RangeSig>(&QCPAxis::rangeChanged),
                this, [this](const QCPRange &r) {
            if (m_xRangeSyncing) return;
            m_xRangeSyncing = true;
            m_waterfall->setXRange(r.lower, r.upper);
            m_xRangeSyncing = false;
        });
        // 时频图 x 轴变化 → 同步到频谱图
        connect(m_waterfall->xAxis, static_cast<RangeSig>(&QCPAxis::rangeChanged),
                this, [this](const QCPRange &r) {
            if (m_xRangeSyncing) return;
            m_xRangeSyncing = true;
            m_spectrum->setXRange(r.lower, r.upper);
            m_xRangeSyncing = false;
        });
        // Worker 完成重算后，直接推送数据到频谱图刷新显示
        connect(m_waterfall, &HQTfwaterfall::maxHoldAvgUpdated,
                m_spectrum, &HQSpectrum::onMaxHoldAvgUpdated);

        // ---- 分隔线2 ----
        m_separator2 = createSep();
        graphLayout->addWidget(m_separator2);

        // ---- 回放进度条 ----
        const int sideMar = scaledPx(16, 10);
        m_progressBar = new RecordProgressBar(m_graphPanel);
        m_progressBar->setSideMargin(sideMar);
        m_progressBar->setTotalTime(0);
        m_progressBar->setPosition(0);
        // 进度拖拽 → seek 到目标时间位置重新读取播放
        connect(m_progressBar, &RecordProgressBar::positionChanged,
                this, &RecordDialog::seekToPosition);
        connect(m_progressBar, &RecordProgressBar::pauseToggled, this, &RecordDialog::slotPause);
        graphLayout->addWidget(m_progressBar);

        m_separator3 = createSep();
        graphLayout->addWidget(m_separator3);

        // ---- 信号列表框 ----
        m_listBox = new HQListBox(m_graphPanel);
        connect(m_listBox, &HQListBox::addSignalNum, this, &RecordDialog::addSignalNum);
        // 时频图最后一帧时间 → 列表行过期清理
        connect(m_waterfall, &HQTfwaterfall::lastFrameTimeUpdated,
                m_listBox, &HQListBox::removeOldRows);

        // 列表行单击 → 频谱图定位/高亮对应标记框
        connect(m_listBox, &HQListBox::rowClickedForMark,
                m_spectrum, &HQSpectrum::onRowSelectedForMark);
        // 列表行单击 → 时频图定位/高亮对应标记框
        connect(m_listBox, &HQListBox::rowClickedForMark,
                m_waterfall, &HQTfwaterfall::onRowSelectedForMark);

        // 频谱图标记框点击 → 列表自动滚动到对应行（反向同步）
        connect(m_spectrum, &HQSpectrum::markClicked,
                m_listBox, &HQListBox::scrollToRowById);
        // 时频图标记框点击 → 列表自动滚动到对应行（反向同步）
        connect(m_waterfall, &HQTfwaterfall::markClicked,
                m_listBox, &HQListBox::scrollToRowById);

        // 频谱图选中变化 → 时频图同步选中（双向，onExternalMarkSelected 不再反向发信号，无循环）
        connect(m_spectrum, &HQSpectrum::markSelectionChanged,
                m_waterfall, &HQTfwaterfall::onExternalMarkSelected);
        connect(m_waterfall, &HQTfwaterfall::markSelectionChanged,
                m_spectrum, &HQSpectrum::onExternalMarkSelected);

        graphLayout->addWidget(m_listBox, 1);

        m_mainLayout->addWidget(m_graphPanel, 1);
    }
}

// ============================================================================
// 主题
// ============================================================================

void RecordDialog::applyTheme()
{
    const auto &tm = ThemeManager::instance();
    const QString borderCol   = tm.colorString("CollMonitor.borderColor");
    const QString panelBg     = QStringLiteral("rgb(6,6,16)");
    const int     panelRadius = 12;

    // ---- 标题栏 ----
    if (m_titleBar)
        m_titleBar->setStyleSheet("#RecordDialogTitleBar { background-color: rgb(14,14,29); }");

    if (m_titleLabel)
        m_titleLabel->setStyleSheet(QString("color: %1; background: transparent; border: none;")
            .arg(tm.color("dialog.titleColor").isValid()
                 ? tm.color("dialog.titleColor").name() : QStringLiteral("#E8E9EB")));

    if (m_accentLine)
        m_accentLine->setStyleSheet(QString("QLabel { background-color: %1; border-radius: 5px; border: none; }")
            .arg(tm.color("dialog.accentLineColor").isValid()
                 ? tm.color("dialog.accentLineColor").name() : QStringLiteral("rgba(10,140,254,255)")));

    // ---- 信息面板（圆角矩形） ----
    if (m_infoPanel)
        m_infoPanel->setStyleSheet(
            QStringLiteral("#RecordDialogInfoPanel {"
                           "  background-color: %1;"
                           "  border: 1px solid %2;"
                           "  border-radius: %3px;"
                           "}")
                .arg(panelBg, borderCol).arg(panelRadius));

    // 内容区背景透明（让面板的背景透出）
    if (m_contentWidget)
        m_contentWidget->setStyleSheet("#RecordDialogContent { background: transparent; }");

    // ---- 信息标签 ----
    QString ls = QString("color: %1; background: transparent; border: none;")
        .arg(tm.color("dialog.infoLabelColor").isValid()
             ? tm.color("dialog.infoLabelColor").name() : QStringLiteral("#909399"));
    QString vs = QString("color: %1; background: transparent; border: none;")
        .arg(tm.color("dialog.infoValueColor").isValid()
             ? tm.color("dialog.infoValueColor").name() : QStringLiteral("#E8E9EB"));

    if (m_contentWidget) {
        for (auto *w : m_contentWidget->findChildren<QLabel *>("RecordDialogInfoLabel"))
            w->setStyleSheet(ls);
        for (auto *w : m_contentWidget->findChildren<QLabel *>("RecordDialogInfoValue"))
            w->setStyleSheet(vs);
    }

    // ---- 统计标签（每个数字使用各自的 collStatPanel 主题色） ----
    auto updateNum = [&](QLabel *lbl, const QString &colorKey) {
        if (!lbl) return;
        const QString c = tm.colorString(colorKey);
        lbl->setStyleSheet(
            QString("color: %1; background: transparent; border: none; font-weight: bold;").arg(c));
    };
    auto updateIconBg = [&](const QString &objName, const QString &colorKey) {
        auto *w = m_infoPanel->findChild<QWidget *>(objName);
        if (!w) return;
        const QString c = tm.colorString(colorKey);
        const int r = static_cast<int>(DPR_REAL(12.0 * ScreenScale::instance().scale(),
                                                ScreenScale::instance().dpr()));
        w->setStyleSheet(QString("background-color: %1; border-radius: %2px;").arg(c).arg(r));
    };
    updateNum(m_severeAlertLabel,  QStringLiteral("collStatPanel.criticalAlertColor"));
    updateNum(m_generalAlertLabel, QStringLiteral("collStatPanel.generalAlarmColor"));
    updateNum(m_totalCountLabel,   QStringLiteral("collStatPanel.signalTotalColor"));
    updateIconBg(QStringLiteral("StatIcon_collStatPanel.criticalAlertColor"),
                 QStringLiteral("collStatPanel.criticalAlertColor"));
    updateIconBg(QStringLiteral("StatIcon_collStatPanel.generalAlarmColor"),
                 QStringLiteral("collStatPanel.generalAlarmColor"));
    updateIconBg(QStringLiteral("StatIcon_collStatPanel.signalTotalColor"),
                 QStringLiteral("collStatPanel.signalTotalColor"));

    // ---- 图表面板（圆角矩形） ----
    if (m_graphPanel)
        m_graphPanel->setStyleSheet(
            QStringLiteral("#RecordDialogGraphPanel {"
                           "  background-color: %1;"
                           "  border: 1px solid %2;"
                           "  border-radius: %3px;"
                           "}")
                .arg(panelBg, borderCol).arg(panelRadius));

    // ---- 分隔线 ----
    const QString sepSs = QStringLiteral("QFrame { border: none; background-color: %1; }").arg(borderCol);
    if (m_separator1) m_separator1->setStyleSheet(sepSs);
    if (m_separator2) m_separator2->setStyleSheet(sepSs);
    if (m_separator3) m_separator3->setStyleSheet(sepSs);

    // ---- 关闭按钮 ----
    if (m_closeBtn)
        m_closeBtn->setStyleSheet(
            QString("QPushButton { background: transparent; border: none; }"
                    "QPushButton:hover { background-color: %1; }"
                    "QPushButton:pressed { background-color: %2; }")
                .arg(tm.color("titleBar.buttonHoverTint").isValid()
                     ? tm.colorString("titleBar.buttonHoverTint") : QStringLiteral("rgba(255,255,255,24)"))
                .arg(tm.color("titleBar.buttonPressedBgColor").isValid()
                     ? tm.colorString("titleBar.buttonPressedBgColor") : QStringLiteral("rgba(255,255,255,24)")));

    update();
}

// ============================================================================
// 拖拽
// ============================================================================

bool RecordDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (!isVisible())
        return QDialog::eventFilter(watched, event);

    if (m_closeBtn && watched == m_closeBtn)
        return QDialog::eventFilter(watched, event);

    if (event->type() == QEvent::MouseButtonPress) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() != Qt::LeftButton)
            return QDialog::eventFilter(watched, event);

        QWidget *w = qobject_cast<QWidget *>(watched);
        if (!w || w->window() != this)
            return QDialog::eventFilter(watched, event);

        const QRect dragZone(0, 0, width(), m_titleBar->geometry().bottom());
        if (dragZone.contains(mapFromGlobal(me->globalPos()))) {
            m_dragging = true;
            m_dragPosition = me->globalPos() - frameGeometry().topLeft();
            return true;
        }

    } else if (event->type() == QEvent::MouseMove) {
        if (!m_dragging)
            return QDialog::eventFilter(watched, event);
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->buttons() & Qt::LeftButton) {
            move(me->globalPos() - m_dragPosition);
            return true;
        }
    } else if (event->type() == QEvent::MouseButtonRelease) {
        if (m_dragging)
            m_dragging = false;
    }

    return QDialog::eventFilter(watched, event);
}

int RecordDialog::scaledPx(int designPx, int min) const
{
    const auto &ss = ScreenScale::instance();
    return DPR_INT(designPx * ss.scale(), ss.dpr(), min);
}

QString RecordDialog::epochToTimeStr(int64_t epochMs)
{
    if (epochMs <= 0) return "-";
    qint64 secs = epochMs / 1000;
    QDateTime dt = QDateTime::fromSecsSinceEpoch(secs);
    return dt.toString("yyyy-MM-dd hh:mm:ss");
}
