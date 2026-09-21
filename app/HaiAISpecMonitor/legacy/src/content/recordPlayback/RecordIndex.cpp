#include "RecordIndex.h"
#include "RecordFileList.h"
#include "details/HQDetailInfo.h"
#include "details/HQDetailList.h"
#include "comm/CommonMacros.h"
#include "comm/ScreenScale.h"
#include "widgets/ToastWidget.h"
#include "widgets/HQMessageBox.h"
#include <QVBoxLayout>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QEasingCurve>
#include <QResizeEvent>
#include <QDateTime>
#include <QShowEvent>
#include <QTimer>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QMessageBox>
#include <QEventLoop>
#include <cmath>
#include "utils.hpp"

static QString csvEscape(const QString& s)
{
    if (s.contains(',') || s.contains('"') || s.contains('\n'))
        return '"' + QString(s).replace("\"", "\"\"") + '"';
    return s;
}

RecordIndex::RecordIndex(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, true);

    initUI();
    initConnects();
}

void RecordIndex::initUI()
{
    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

    // 内边距 12px（设计稿 2x 基准），转换为逻辑像素后保存
    const int padInt = static_cast<int>(DPR_REAL(12.0 * scale, dpr));

    auto contentLayout = new QVBoxLayout(this);
    contentLayout->setContentsMargins(padInt, padInt, padInt, padInt);
    contentLayout->setSpacing(0);

    // 页面容器（不使用布局，以便手动控制位置实现滑动切换动画）
    m_pageContainer = new QWidget(this);
    m_pageContainer->installEventFilter(this);

    // ---- 页面 0：录制文件列表 ----
    m_recordFileList = new RecordFileList(m_pageContainer);
    m_recordFileList->show();

    // ---- 页面 1：详情页 ----
    m_detailPage = new QWidget(m_pageContainer);
    auto *detailLayout = new QVBoxLayout(m_detailPage);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    detailLayout->setSpacing(DPR_INT(12 * scale, dpr, 6));

    m_detailInfo = new HQDetailInfo(QStringList(), m_detailPage);
    m_detailList = new HQDetailList(QStringList(), m_detailPage);

    detailLayout->addWidget(m_detailInfo, 1);
    detailLayout->addWidget(m_detailList, 5);

    m_detailPage->hide();

    contentLayout->addWidget(m_pageContainer, 1);
}

void RecordIndex::initConnects()
{
    // 列表页详情按钮 → 切换至详情页，同时发送信号明细查询请求
    connect(m_recordFileList, &RecordFileList::detailClicked,
            this, [this](const QStringList &rowData) {
        slideToPage(1, rowData);
        // 从 rowData 末位提取 file_id 并发送查询请求
        qint64 fid = 0;
        if (rowData.size() > 12) {
            bool ok = false;
            fid = rowData[12].toLongLong(&ok);
            if (!ok) fid = 0;
        }
        if (fid != 0) {
            SignalDetailPerFileQueryReq req;
            req.file_id = fid;
            emit signalDetailPerFileQueryReq(req);
        }
    });
    // 文件回放信号列表查询
    connect(m_recordFileList, &RecordFileList::dialogSignalDetailSearch, this, &RecordIndex::dialogSignalDetailSearch);
    connect(this, &RecordIndex::dialogSignalDetail, m_recordFileList, &RecordFileList::dialogSignalDetail);

    // 详情页返回按钮 → 切回列表页
    connect(m_detailInfo, &HQDetailInfo::backClicked,
            this, [this]() {
        slideToPage(0);
    });

    // 列表页底部"清理"按钮 → 删除所有勾选文件
    connect(m_recordFileList, &RecordFileList::deleteClicked,
            this, [this]() {
        auto ids = m_recordFileList->getCheckedFileIds();
        if (ids.isEmpty()) {
            ToastWidget::instance()->showInfo(u8"请先勾选要删除的文件");
            return;
        }
        auto* box = HQMessageBox::getInstance();
        box->setTitle(u8"确认删除");
        box->setMsgInfo(QStringLiteral("确认删除选中的 %1 个文件？删除后不可恢复。").arg(ids.size()));
        if (box->exec() != QDialog::Accepted)
            return;
        doDeleteFiles(ids);
    });

    // 列表页行删除按钮
    connect(m_recordFileList, &RecordFileList::rowDeleteClicked,
            this, [this](int row) {
        qint64 fid = m_recordFileList->getRowFileId(row);
        if (fid == 0) return;
        auto* box = HQMessageBox::getInstance();
        box->setTitle(u8"确认删除");
        box->setMsgInfo(u8"确认删除该文件？删除后不可恢复。");
        if (box->exec() != QDialog::Accepted)
            return;
        doDeleteFiles({ fid });
    });

    // 导出按钮
    connect(m_recordFileList, &RecordFileList::exportClicked,
            this, &RecordIndex::onExportFileList);
    connect(m_detailList, &HQDetailList::exportClicked,
            this, &RecordIndex::onExportDetail);

    // 信号删除请求
    connect(m_detailList, &HQDetailList::deleteSignalsRequested,
            this, &RecordIndex::doDeleteSignals);
}

void RecordIndex::spectrumFileDelResp(const SpectrumFileDelResp& resp) const
{
    if (resp.success == 0) {
        ToastWidget::instance()->showInfo(u8"文件删除成功");
        const_cast<RecordIndex*>(this)->emit requestFileListRefresh();
    } else {
        ToastWidget::instance()->showInfo(u8"文件删除失败");
    }
}

void RecordIndex::signalDelResp(const SignalDetailPerFileDelResp& resp) const
{
    if (resp.success == 0) {
        ToastWidget::instance()->showInfo(u8"信号删除成功");
        // 重新查询当前文件的信号明细以刷新详情列表
        if (m_currentFileId != 0) {
            SignalDetailPerFileQueryReq req;
            req.file_id = m_currentFileId;
            const_cast<RecordIndex*>(this)->emit signalDetailPerFileQueryReq(req);
        }
        // 刷新文件列表（signal_count / alarm_count 已更新）
        const_cast<RecordIndex*>(this)->emit requestFileListRefresh();
    } else {
        ToastWidget::instance()->showInfo(u8"信号删除失败");
    }
}

void RecordIndex::onExportFileList()
{
    auto checkedIds = m_recordFileList->getCheckedFileIds();
    if (checkedIds.isEmpty()) {
        ToastWidget::instance()->showInfo(u8"请先勾选要导出的文件");
        return;
    }

    QString dir = QFileDialog::getExistingDirectory(this, u8"选择导出目录");
    if (dir.isEmpty()) return;

    // 从文件列表表格读取勾选文件数据写入 spectrum_files.csv
    QFile file(dir + "/spectrum_files.csv");
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, u8"导出失败", u8"无法创建 spectrum_files.csv");
        return;
    }
    QTextStream out(&file);
    out.setCodec("UTF-8");
    out << "file_id,file_name,source_name,fc,span,rbw,ref_level,"
        << "bgn_time,end_time,size,signal_num,alarm_num,len_per_spec,spec_num\n";

    int exportedCount = 0;
    auto all_infos = m_recordFileList->getAllRowData();
    for (const auto& row : all_infos) {
        qint64 fid = row.value(12).toLongLong();
        if (!checkedIds.contains(fid))
            continue;
        // row: [index, file_name, device, startF, endF, rbw, startT, endT, totalT, size, signal_num, alert_num, file_id]
        out << fid << ","
            << csvEscape(row.value(1)) << ","                // file_name
            << csvEscape(row.value(2)) << ","                // source_name
            << row.value(3).toDouble() * 1e6 << ","          // fc (MHz → Hz)
            << (row.value(4).toDouble() - row.value(3).toDouble()) * 1e6 << ","  // span (Hz)
            << row.value(5).toInt() * 1000 << ","            // rbw (kHz → Hz)
            << "0,"                                          // ref_level (not available)
            << csvEscape(row.value(6)) << ","                // bgn_time (display format)
            << csvEscape(row.value(7)) << ","                // end_time (display format)
            << row.value(9).replace(" MB", "").toInt() << ","  // size (MB)
            << row.value(10).toInt() << ","                   // signal_num
            << row.value(11).toInt() << ","                   // alarm_num
            << "0,0\n";                                      // len_per_spec, spec_num
        ++exportedCount;
    }
    file.close();

    // 为所有勾选的文件逐一查询并导出信号明细
    int detailExportedCount = 0;
    for (qint64 exportFileId : checkedIds) {
        // 从文件列表中查找导出文件的名字，用于信号CSV文件的前缀
        QString exportFileName;
        for (const auto& row : all_infos) {
            if (row.value(12).toLongLong() == exportFileId) {
                exportFileName = row.value(1);
                break;
            }
        }
        // 如果当前详情页数据不是目标文件，重新查询
        if (m_detailList->currentFileId() != exportFileId || !m_detailList->hasSignalData()) {
            m_detailList->clear();
            // 提前设置文件名，确保信号CSV前缀正确
            m_detailList->setData({QString(), exportFileName});
            QEventLoop loop;
            QTimer::singleShot(10000, &loop, [&loop]() { loop.quit(); }); // 10秒超时
            auto conn = connect(this, &RecordIndex::signalDetailPerFileQueryResp,
                                &loop, [&loop, exportFileId](const SignalDetailPerFileQueryResp& resp) {
                if (resp.file_id == exportFileId)
                    loop.quit();
            });
            SignalDetailPerFileQueryReq req;
            req.file_id = exportFileId;
            emit signalDetailPerFileQueryReq(req);
            loop.exec();
            disconnect(conn);
        }
        // 确认数据已加载到目标文件后再导出
        if (m_detailList->currentFileId() == exportFileId && m_detailList->hasSignalData()) {
            if (m_detailList->exportToCsv(dir))
                ++detailExportedCount;
        }
    }

    QString msg = QStringLiteral("已导出 %1 条文件记录到\n%2").arg(exportedCount).arg(dir);
    if (detailExportedCount > 0)
        msg += QStringLiteral("\n（同时包含 %1 个文件的信号明细）").arg(detailExportedCount);
    QMessageBox::information(this, u8"导出完成", msg);
}

void RecordIndex::onExportDetail()
{
    QString dir = QFileDialog::getExistingDirectory(this, u8"选择导出目录");
    if (dir.isEmpty()) return;

    if (!m_detailList->exportToCsv(dir)) {
        ToastWidget::instance()->showInfo(u8"没有可导出的信号数据");
        return;
    }

    QMessageBox::information(this, u8"导出完成",
        QStringLiteral("已导出信号明细到\n%1").arg(dir));
}

void RecordIndex::dialogSignalDetailSearch(int64_t fileId)
{
    SignalDetailPerFileQueryReq req;
    req.file_id = fileId;
    emit signalDetailPerFileQueryReq(req);
}

void RecordIndex::openRecordDialog()
{
    m_recordFileList->openRecordDialog();
}

void RecordIndex::setSignalPath(const QString& signalPath)
{
    SpectrumFileInfos infos;
    infos.num = 0;
    infos.items = nullptr;
    spectrumFileInfos(infos); // 清空上一次的状态
    m_recordFileList->setSignalPath(signalPath);
}

bool RecordIndex::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_pageContainer && event->type() == QEvent::Resize) {
        // 页面容器大小变化时，同步调整两个页面的大小
        QResizeEvent *re = static_cast<QResizeEvent *>(event);
        QSize size = re->size();
        m_recordFileList->resize(size);
        m_detailPage->resize(size);
        return true;
    }
    return QWidget::eventFilter(obj, event);
}

void RecordIndex::slideToPage(int index, const QStringList &rowData)
{
    // 动画进行中，记下待切换目标，等当前动画完成后再处理
    if (m_animating) {
        m_pendingIndex = index;
        if (index == 1)
            updateDetailData(rowData);
        return;
    }

    // 目标页面就是当前页面，不做切换
    if (index == m_currentPageIndex)
        return;

    m_animating = true;
    m_pendingIndex = -1;

    // 传入详情页数据
    if (index == 1)
        updateDetailData(rowData);

    QWidget *currentPage = (m_currentPageIndex == 0)
                               ? static_cast<QWidget *>(m_recordFileList)
                               : static_cast<QWidget *>(m_detailPage);
    QWidget *nextPage    = (index == 0)
                               ? static_cast<QWidget *>(m_recordFileList)
                               : static_cast<QWidget *>(m_detailPage);

    int width = m_pageContainer->width();

    // 宽度为 0（启动时尚未布局完成），直接切换跳过动画
    if (width <= 0) {
        currentPage->hide();
        nextPage->show();
        m_currentPageIndex = index;
        m_animating = false;
        return;
    }

    // 切换方向：列表页→详情页 向左滑 (-1)，详情页→列表页 向右滑 (+1)
    int direction = (index > m_currentPageIndex) ? -1 : 1;

    // 将新页面放在容器外起始位置，并显示（此时新旧两页同时可见）
    nextPage->setGeometry(direction * width, 0, width, m_pageContainer->height());
    nextPage->show();
    nextPage->raise();

    // 创建并行动画组：当前页滑出 & 新页面滑入
    auto *group = new QParallelAnimationGroup(this);

    auto *slideOut = new QPropertyAnimation(currentPage, "pos");
    slideOut->setDuration(300);
    slideOut->setStartValue(QPoint(0, 0));
    slideOut->setEndValue(QPoint(-direction * width, 0));
    slideOut->setEasingCurve(QEasingCurve::InOutCubic);

    auto *slideIn = new QPropertyAnimation(nextPage, "pos");
    slideIn->setDuration(300);
    slideIn->setStartValue(QPoint(direction * width, 0));
    slideIn->setEndValue(QPoint(0, 0));
    slideIn->setEasingCurve(QEasingCurve::InOutCubic);

    group->addAnimation(slideOut);
    group->addAnimation(slideIn);

    // 动画结束后：隐藏旧页面、复位位置、更新当前索引、处理待切换
    connect(group, &QParallelAnimationGroup::finished, this, [=]() {
        currentPage->hide();
        currentPage->move(0, 0);
        m_currentPageIndex = index;
        m_animating = false;
        m_pageContainer->update();

        // 动画期间若有新的切换请求，继续执行
        if (m_pendingIndex >= 0 && m_pendingIndex != m_currentPageIndex) {
            int nextPending = m_pendingIndex;
            m_pendingIndex = -1;
            slideToPage(nextPending);
        }
    });

    group->start(QAbstractAnimation::DeleteWhenStopped);
}

void RecordIndex::updateDetailData(const QStringList &rowData)
{
    // 末位元素为 file_id，提取后传给详情页
    m_currentFileId = 0;
    if (rowData.size() > 12) {
        bool ok = false;
        m_currentFileId = rowData[12].toLongLong(&ok);
        if (!ok) m_currentFileId = 0;
    }

    // 传给详情页的数据去掉末位 file_id
    QStringList displayData = rowData.mid(0, 12);
    m_detailInfo->setData(displayData);
    m_detailList->setData(displayData);
}

int RecordIndex::minimumContentWidth() const
{
    const auto &ss = ScreenScale::instance();
    return static_cast<int>(DPR_REAL(800.0 * ss.scale(), ss.dpr()));
}

void RecordIndex::spectrumFileInfo(const SpectrumFileInfo& info) const
{
    auto* list = const_cast<RecordFileList*>(m_recordFileList);
    int pos = list->findFileIndex(info.file_id);
    int idx = (pos >= 0) ? (pos + 1) : (list->getAllRowData().size() + 1);
    QStringList row = fileInfoToRowData(info, idx);
    list->upsertRow(row, info, info.file_id);
}

void RecordIndex::spectrumFileInfos(const SpectrumFileInfos& infos) const
{
    auto* list = const_cast<RecordFileList*>(m_recordFileList);
    int prevPage = list->currentPage();
    QSet<qint64> savedChecked = list->checkedFileIdSet();
    list->clearList();
    for (int32_t i = 0; i < infos.num; ++i) {
        auto& info = infos.items[infos.num - 1 - i];
        QStringList row = fileInfoToRowData(info, i + 1);
        list->addRow(row, info, info.file_id);
    }
    list->setTotalCount(infos.num);
    int pageSize = list->pageSize();
    int totalPage = (infos.num + pageSize - 1) / pageSize;
    if (totalPage < 1) totalPage = 1;
    list->setTotalPage(totalPage);
    // 恢复切换页面前的勾选状态
    list->setCheckedFileIds(savedChecked);
    list->setCurrentPage(prevPage);
    // 如果之前的页面已失效（超出总页数），setCurrentPage 内部会自动修正到最后一页
}

void RecordIndex::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    // 切换到录制回放界面时请求后端刷新文件列表
    emit requestFileListRefresh();
}



void RecordIndex::signalDetailPerFileQueryResp(const SignalDetailPerFileQueryResp& resp)
{
    // 构建全量数据（不直接插入表格，由 HQDetailList 分页按需渲染）
    QVector<QStringList> allData;
    QVector<QVector<QStringList>> allTimes;
    allData.reserve(resp.signal_num);
    allTimes.reserve(resp.signal_num);
    for (int32_t i = 0; i < resp.signal_num; ++i) {
        auto& sig = resp.item[i];

        QStringList row;
        row << QString::number(sig.id);                                    // ColID
        row << QString::number(sig.fc / 1e6, 'f', 6);       // ColFreq
        row << QString::number(sig.bw / 1e3, 'f', 3);       // ColBandwidth

        if (0 == sig.carry_type) 
        {
            row << QStringLiteral("常在");                                        // ColSignalType
        }else
        {
            row << QStringLiteral("突发");                                        // ColSignalType
        }
        
        row << QString::fromStdString(utils::timestampMsToString(sig.recent_time / NS_PER_MS));                // ColRecentCount

        row << QString::number(sig.burst_num);                             // ColTotalCount

        row << QString::number((sig.avg_duration / NS_PER_MS )/ 1000.0, 'f', 3);        // ColAvgCount (秒)
        
        row << (sig.alarm_level > 0 ? QStringLiteral("告警") : QStringLiteral("正常")); // ColAlertStatus
        allData.append(row);

        // 时间线数据
        QVector<QStringList> times;
        times.reserve(sig.burst_num);
        for (int32_t j = 0; j < sig.burst_num; ++j) { 
            QStringList t;
            t << QString::fromStdString(utils::timestampMsToString(sig.times[j].bgn_time / NS_PER_MS));
            t << QString::fromStdString(utils::timestampMsToString(sig.times[j].end_time / NS_PER_MS));
            int64_t durMs = (sig.times[j].end_time - sig.times[j].bgn_time) / NS_PER_MS;
            t << QString::number(durMs / 1000.0, 'f', 3) + "s";
            t << QString::number(sig.fc / 1e6, 'f', 6) + " MHz";
            t << QString::number(sig.bw / 1e3, 'f', 3) + " kHz";
            times.append(t);
        }
        allTimes.append(times);
    }

    // 保存原始信号数据用于导出CSV
    QVector<HQDetailList::RawSignalItem> rawItems;
    rawItems.reserve(resp.signal_num);
    for (int32_t i = 0; i < resp.signal_num; ++i) {
        auto& src = resp.item[i];
        HQDetailList::RawSignalItem item;
        item.id = src.id;
        item.fc = src.fc;
        item.bw = src.bw;
        item.carry_type = src.carry_type;
        item.recent_time = src.recent_time;
        item.burst_num = src.burst_num;
        item.avg_duration = src.avg_duration;
        item.times.reserve(src.burst_num);
        for (int32_t j = 0; j < src.burst_num; ++j) {
            item.times.append({src.times[j].bgn_time, src.times[j].end_time});
        }
        rawItems.append(item);
    }

    auto* list = const_cast<HQDetailList*>(m_detailList);
    int prevPage = list->currentPage();
    list->setSignalData(allData);
    list->setDetailTimeData(allTimes);
    list->setRawSignalData(rawItems);
    list->setCurrentFileId(resp.file_id);
    list->setCurrentPage(prevPage);
    // 若 prevPage 超出新的总页数，setCurrentPage 内部会自动修正到最后一页

    // 设置回放的信号列表信息
    emit dialogSignalDetail(resp);

    // 释放 CustomWindow::signalDetailPerFileQueryResp 中深拷贝分配的堆内存。
    // emit dialogSignalDetail 是 DirectConnection（同线程），返回时所有接收者
    // 已使用完毕，释放是安全的。
    if (resp.item != nullptr && resp.signal_num > 0) {
        for (int32_t i = 0; i < resp.signal_num; ++i) {
            delete[] resp.item[i].times;
        }
        delete[] resp.item;
    }
}

void RecordIndex::doDeleteSignals(qint64 fileId, const QVector<qint64>& signalIds)
{
    if (signalIds.isEmpty() || fileId == 0) return;

    SignalDetailPerFileDelReq req;
    req.file_id = fileId;
    req.signal_num = signalIds.size();
    req.signal_ids = new int64_t[signalIds.size()];
    for (int i = 0; i < signalIds.size(); ++i)
        req.signal_ids[i] = signalIds[i];

    emit signalDelReq(req);
}

void RecordIndex::doDeleteFiles(const QVector<qint64>& fileIds)
{
    if (fileIds.isEmpty()) return;

    // 从勾选集合中移除被删除的文件ID并立即更新已选计数，
    // 否则刷新列表时 savedChecked 会保留已删除ID，导致计数不减少
    QSet<qint64> idSet;
    for (qint64 fid : fileIds)
        idSet.insert(fid);
    m_recordFileList->removeCheckedFileIds(idSet);

    SpectrumFileDelReq req;
    req.file_num = fileIds.size();
    req.file_ids = new int64_t[fileIds.size()];
    for (int i = 0; i < fileIds.size(); ++i)
        req.file_ids[i] = fileIds[i];

    emit spectrumFileDelReq(req);

    // 请求发出后回退到列表页
    if (m_currentPageIndex == 1)
        const_cast<RecordIndex*>(this)->slideToPage(0);
}

QStringList RecordIndex::fileInfoToRowData(const SpectrumFileInfo& info, int index) const
{
    QStringList row;
    row << QString::number(index);                                          // ColIndex
    row << QString::fromUtf8(info.file_name);                               // ColFileName
    row << QString::fromUtf8(info.source_name);                             // ColDevice
    row << QString::number((info.fc - info.span / 2) / 1e6, 'f', 3);        // ColStartF (中心频率)
    row << QString::number((info.fc + info.span / 2) / 1e6, 'f', 3);        // ColEndF
    row << QString::number(info.rbw / 1000);                                // ColRBW (kHz)
    row << QString::fromStdString(utils::timestampMsToString(info.bgn_time / NS_PER_MS));                                   // ColStartT
    row << QString::fromStdString(utils::timestampMsToString(info.end_time / NS_PER_MS));                                   // ColEndT

   
    // 总时长
    {
        int64_t totalSec = (info.end_time - info.bgn_time) / 1e9;
        if (totalSec < 0) totalSec = 0;
        row << formatDuration(totalSec);                                    // ColTotalT
    }
    row << QString::number(info.size) + " MB";                              // ColSize
    row << QString::number(info.signal_num);                                // ColSignalNum
    row << QString::number(info.alarm_num);                                 // ColAlertNum
    return row;
}

QString RecordIndex::epochToTimeStr(int64_t epochMs)
{
    if (epochMs <= 0) return "-";
    qint64 secs = epochMs / 1000;
    QDateTime dt = QDateTime::fromSecsSinceEpoch(secs);
    return dt.toString("yyyy-MM-dd hh:mm:ss");
}

QString RecordIndex::formatDuration(int64_t seconds)
{
    int64_t min = seconds / 60;
    int64_t sec = seconds % 60;
    return QStringLiteral("%1分%2秒").arg(min).arg(sec);
}
