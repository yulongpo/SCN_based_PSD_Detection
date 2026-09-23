#include "PlaybackPage.h"

#include <QAbstractItemView>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextStream>
#include <QVBoxLayout>

#include <algorithm>

namespace scn::app
{

namespace
{
QPushButton* toolButton(const QString& text, QWidget* parent, const QString& iconPath = {})
{
    auto* button = new QPushButton(text, parent);
    button->setObjectName(QStringLiteral("pageToolButton"));
    button->setMinimumHeight(32);
    if (!iconPath.isEmpty()) {
        button->setIcon(QIcon(iconPath));
        button->setIconSize(QSize(16, 16));
    }
    return button;
}

QPushButton* rowIconButton(const QString& iconPath, const QString& tip, QWidget* parent)
{
    auto* button = new QPushButton(parent);
    button->setIcon(QIcon(iconPath));
    button->setIconSize(QSize(16, 16));
    button->setFixedSize(24, 24);
    button->setFlat(true);
    button->setToolTip(tip);
    button->setStyleSheet(QStringLiteral(
        "QPushButton { background:transparent; border:0; }"
        "QPushButton:hover { background:rgba(255,255,255,25); border-radius:3px; }"));
    return button;
}

QTableWidgetItem* item(const QString& text)
{
    auto* result = new QTableWidgetItem(text);
    result->setTextAlignment(Qt::AlignCenter);
    return result;
}
}

PlaybackPage::PlaybackPage(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
}

void PlaybackPage::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 12);
    root->setSpacing(10);
    m_pages = new QStackedWidget(this);
    buildListPage();
    buildDetailPage();
    m_pages->addWidget(m_listPage);
    m_pages->addWidget(m_detailPage);
    root->addWidget(m_pages);
}

void PlaybackPage::buildListPage()
{
    m_listPage = new QWidget(m_pages);
    auto* root = new QVBoxLayout(m_listPage);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(10);

    auto* heading = new QHBoxLayout;
    auto* title = new QLabel(QStringLiteral("回放文件列表"), m_listPage);
    title->setObjectName(QStringLiteral("pageTitle"));
    auto* hint = new QLabel(QStringLiteral("查看已保存的频谱文件和信号明细"), m_listPage);
    hint->setObjectName(QStringLiteral("pageHint"));
    heading->addWidget(title);
    heading->addSpacing(14);
    heading->addWidget(hint);
    heading->addStretch();
    root->addLayout(heading);

    auto* toolbar = new QHBoxLayout;
    toolbar->addWidget(new QLabel(QStringLiteral("文件筛选"), m_listPage));
    m_filterEdit = new QLineEdit(m_listPage);
    m_filterEdit->setPlaceholderText(QStringLiteral("按文件名、设备或路径搜索"));
    m_filterEdit->setMinimumHeight(34);
    toolbar->addWidget(m_filterEdit, 1);
    auto* importButton = toolButton(QStringLiteral("导入文件"), m_listPage,
                                    QStringLiteral(":/recordplayback/custom.png"));
    auto* exportButton = toolButton(QStringLiteral("导出列表"), m_listPage,
                                    QStringLiteral(":/recordplayback/export.png"));
    auto* detailButton = toolButton(QStringLiteral("查看详情"), m_listPage,
                                    QStringLiteral(":/recordplayback/info.png"));
    auto* deleteButton = toolButton(QStringLiteral("删除"), m_listPage,
                                    QStringLiteral(":/recordplayback/delete.png"));
    toolbar->addWidget(importButton);
    toolbar->addWidget(exportButton);
    toolbar->addWidget(detailButton);
    toolbar->addWidget(deleteButton);
    root->addLayout(toolbar);

    m_fileTable = new QTableWidget(0, 13, m_listPage);
    m_fileTable->setObjectName(QStringLiteral("isaTable"));
    m_fileTable->setHorizontalHeaderLabels({
        QStringLiteral("序号"), QStringLiteral("文件名"), QStringLiteral("采集设备"),
        QStringLiteral("起始频率(MHz)"), QStringLiteral("终止频率(MHz)"),
        QStringLiteral("RBW(kHz)"), QStringLiteral("开始时间"), QStringLiteral("结束时间"),
        QStringLiteral("总时长"), QStringLiteral("大小"), QStringLiteral("信号数"), QStringLiteral("告警数"),
        QStringLiteral("操作")
    });
    m_fileTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_fileTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_fileTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_fileTable->setAlternatingRowColors(true);
    m_fileTable->verticalHeader()->setVisible(false);
    m_fileTable->setShowGrid(true);
    m_fileTable->setFocusPolicy(Qt::NoFocus);
    m_fileTable->horizontalHeader()->setStretchLastSection(false);
    m_fileTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_fileTable->horizontalHeader()->setSectionResizeMode(12, QHeaderView::ResizeToContents);
    m_fileTable->verticalHeader()->setDefaultSectionSize(34);
    root->addWidget(m_fileTable, 1);

    auto* footer = new QHBoxLayout;
    m_recordCount = new QLabel(QStringLiteral("共 0 条"), m_listPage);
    m_recordCount->setObjectName(QStringLiteral("pageHint"));
    footer->addWidget(m_recordCount);
    footer->addStretch();
    auto* refresh = toolButton(QStringLiteral("刷新列表"), m_listPage,
                               QStringLiteral(":/recordplayback/custom.png"));
    footer->addWidget(refresh);
    root->addLayout(footer);

    connect(importButton, &QPushButton::clicked, this, &PlaybackPage::importFile);
    connect(exportButton, &QPushButton::clicked, this, &PlaybackPage::exportList);
    connect(detailButton, &QPushButton::clicked, this, &PlaybackPage::showDetails);
    connect(deleteButton, &QPushButton::clicked, this, &PlaybackPage::deleteSelected);
    connect(refresh, &QPushButton::clicked, this, &PlaybackPage::refreshTable);
    connect(m_filterEdit, &QLineEdit::textChanged, this, &PlaybackPage::filterRows);
    connect(m_fileTable, &QTableWidget::cellDoubleClicked,
            this, [this](int, int) { openCurrentRow(); });
}

void PlaybackPage::buildDetailPage()
{
    m_detailPage = new QWidget(m_pages);
    auto* root = new QVBoxLayout(m_detailPage);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(10);

    auto* heading = new QHBoxLayout;
    auto* backButton = toolButton(QStringLiteral("返回文件列表"), m_detailPage);
    m_detailTitle = new QLabel(QStringLiteral("信号明细"), m_detailPage);
    m_detailTitle->setObjectName(QStringLiteral("pageTitle"));
    heading->addWidget(backButton);
    heading->addSpacing(12);
    heading->addWidget(m_detailTitle);
    heading->addStretch();
    m_exportSignalsButton = toolButton(QStringLiteral("导出信号列表"), m_detailPage,
                                       QStringLiteral(":/recordplayback/export.png"));
    heading->addWidget(m_exportSignalsButton);
    root->addLayout(heading);

    m_detailSummary = new QLabel(m_detailPage);
    m_detailSummary->setObjectName(QStringLiteral("detailSummary"));
    m_detailSummary->setWordWrap(true);
    root->addWidget(m_detailSummary);

    m_signalTable = new QTableWidget(0, 7, m_detailPage);
    m_signalTable->setObjectName(QStringLiteral("isaTable"));
    m_signalTable->setHorizontalHeaderLabels({
        QStringLiteral("ID"), QStringLiteral("中心频率(MHz)"), QStringLiteral("带宽(kHz)"),
        QStringLiteral("信号类型"), QStringLiteral("告警等级"), QStringLiteral("最近出现时间"),
        QStringLiteral("出现次数")
    });
    m_signalTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_signalTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    root->addWidget(m_signalTable, 1);
    connect(backButton, &QPushButton::clicked, this, &PlaybackPage::backToList);
    connect(m_exportSignalsButton, &QPushButton::clicked,
            this, &PlaybackPage::exportCurrentSignals);
}

void PlaybackPage::rememberFile(const QString& path, const QString& sourceName,
                                double startFrequencyHz, double endFrequencyHz,
                                double resolutionBandwidthHz, int signalCount,
                                int alarmCount)
{
    if (path.isEmpty()) return;
    for (auto& record : m_records) {
        if (record.path != path) continue;
        const bool changed = startFrequencyHz != 0.0 && endFrequencyHz != 0.0 &&
            (record.startFrequencyHz != startFrequencyHz ||
             record.endFrequencyHz != endFrequencyHz ||
             record.rbwHz != resolutionBandwidthHz ||
             record.signalCount != signalCount ||
             record.alarmCount != alarmCount);
        const qint64 currentSize = QFileInfo(path).exists() ? QFileInfo(path).size() : 0;
        if (changed || (currentSize > 0 && currentSize != record.sizeBytes)) {
            record.startFrequencyHz = startFrequencyHz;
            record.endFrequencyHz = endFrequencyHz;
            record.rbwHz = resolutionBandwidthHz;
            record.signalCount = signalCount;
            record.alarmCount = alarmCount;
            record.sizeBytes = currentSize;
            if (currentSize > 0) record.end = QDateTime::currentDateTime();
            refreshTable();
        }
        return;
    }
    QFileInfo info(path);
    PlaybackRecord record;
    record.path = path;
    record.fileName = info.fileName();
    record.source = sourceName;
    record.startFrequencyHz = startFrequencyHz;
    record.endFrequencyHz = endFrequencyHz;
    record.rbwHz = resolutionBandwidthHz;
    record.begin = QDateTime::currentDateTime();
    record.end = record.begin;
    record.sizeBytes = info.exists() ? info.size() : 0;
    record.signalCount = signalCount;
    record.alarmCount = alarmCount;
    appendRecord(record);
    emit logMessage(QStringLiteral("已加入回放记录：%1").arg(record.fileName));
}

void PlaybackPage::updateResultSignals(const QString& path, const QVector<PlaybackSignalRow>& rows)
{
    if (path.isEmpty()) return;
    for (auto& record : m_records) {
        if (record.path != path) continue;
        record.signalRows = rows;
        record.signalCount = rows.size();
        refreshTable();
        if (m_detailRecordPath == path) populateDetails(record);
        return;
    }
}

void PlaybackPage::appendRecord(const PlaybackRecord& record)
{
    m_records.push_back(record);
    refreshTable();
}

int PlaybackPage::selectedRow() const
{
    if (!m_fileTable || !m_fileTable->selectionModel()) return -1;
    const auto rows = m_fileTable->selectionModel()->selectedRows();
    return rows.isEmpty() ? -1 : rows.first().row();
}

QString PlaybackPage::formatDuration(const QDateTime& begin, const QDateTime& end) const
{
    const qint64 seconds = qMax<qint64>(0, begin.secsTo(end));
    return QStringLiteral("%1分%2秒").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

void PlaybackPage::refreshTable()
{
    if (!m_fileTable) return;
    m_fileTable->setRowCount(0);
    const QString filter = m_filterEdit ? m_filterEdit->text().trimmed() : QString();
    int index = 0;
    for (const auto& record : m_records) {
        const QString haystack = record.path + record.fileName + record.source;
        if (!filter.isEmpty() && !haystack.contains(filter, Qt::CaseInsensitive)) continue;
        const int row = m_fileTable->rowCount();
        m_fileTable->insertRow(row);
        m_fileTable->setItem(row, 0, item(QString::number(++index)));
        m_fileTable->setItem(row, 1, item(record.fileName));
        m_fileTable->setItem(row, 2, item(record.source));
        m_fileTable->setItem(row, 3, item(QString::number(record.startFrequencyHz / 1e6, 'f', 3)));
        m_fileTable->setItem(row, 4, item(QString::number(record.endFrequencyHz / 1e6, 'f', 3)));
        m_fileTable->setItem(row, 5, item(QString::number(record.rbwHz / 1e3, 'f', 3)));
        m_fileTable->setItem(row, 6, item(record.begin.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"))));
        m_fileTable->setItem(row, 7, item(record.end.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"))));
        m_fileTable->setItem(row, 8, item(formatDuration(record.begin, record.end)));
        m_fileTable->setItem(row, 9, item(record.sizeBytes > 0
            ? QStringLiteral("%1 MB").arg(record.sizeBytes / 1024.0 / 1024.0, 0, 'f', 2)
            : QStringLiteral("-")));
        m_fileTable->setItem(row, 10, item(QString::number(record.signalCount)));
        m_fileTable->setItem(row, 11, item(QString::number(record.alarmCount)));
        m_fileTable->item(row, 0)->setData(Qt::UserRole, record.path);

        auto* operation = new QWidget(m_fileTable);
        auto* operationLayout = new QHBoxLayout(operation);
        operationLayout->setContentsMargins(4, 2, 4, 2);
        operationLayout->setSpacing(4);
        auto* detail = rowIconButton(QStringLiteral(":/recordplayback/info.png"), QStringLiteral("详情"), operation);
        auto* play = rowIconButton(QStringLiteral(":/recordplayback/play.png"), QStringLiteral("播放"), operation);
        auto* exportButton = rowIconButton(QStringLiteral(":/recordplayback/export.png"), QStringLiteral("导出"), operation);
        auto* remove = rowIconButton(QStringLiteral(":/recordplayback/delete.png"), QStringLiteral("删除"), operation);
        operationLayout->addWidget(detail);
        operationLayout->addWidget(play);
        operationLayout->addWidget(exportButton);
        operationLayout->addWidget(remove);
        m_fileTable->setCellWidget(row, 12, operation);
        const QString recordPath = record.path;
        connect(detail, &QPushButton::clicked, this, [this, recordPath] {
            for (const auto& candidate : m_records) {
                if (candidate.path == recordPath) {
                    populateDetails(candidate);
                    m_pages->setCurrentWidget(m_detailPage);
                    return;
                }
            }
        });
        connect(play, &QPushButton::clicked, this, [this, recordPath] {
            emit replayRequested(recordPath);
        });
        connect(exportButton, &QPushButton::clicked, this, &PlaybackPage::exportList);
        connect(remove, &QPushButton::clicked, this, [this, recordPath] {
            for (int i = 0; i < m_records.size(); ++i) {
                if (m_records.at(i).path == recordPath) {
                    m_records.removeAt(i);
                    refreshTable();
                    emit logMessage(QStringLiteral("已从列表移除回放记录。"));
                    return;
                }
            }
        });
    }
    if (m_recordCount) m_recordCount->setText(QStringLiteral("共 %1 条").arg(index));
}

void PlaybackPage::importFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("导入频谱文件"), QString(),
        QStringLiteral("Spectrum files (*.bin *.dat *.txt *.csv *.asc);;All files (*.*)"));
    if (path.isEmpty()) return;
    rememberFile(path, QStringLiteral("FILE"), 0.0, 0.0, 0.0);
    emit replayRequested(path);
}

void PlaybackPage::exportList()
{
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出录制列表"), QStringLiteral("spectrum_records.csv"),
        QStringLiteral("CSV files (*.csv)"));
    if (path.isEmpty()) return;
    QFile output(path);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("导出失败"), QStringLiteral("无法写入目标文件。"));
        return;
    }
    QTextStream stream(&output);
    stream << "index,file,source,start_mhz,end_mhz,rbw_khz,begin,end,signal_count,alarm_count\n";
    for (int i = 0; i < m_records.size(); ++i) {
        const auto& record = m_records.at(i);
        stream << i + 1 << ',' << record.fileName << ',' << record.source << ','
               << record.startFrequencyHz / 1e6 << ',' << record.endFrequencyHz / 1e6 << ','
               << record.rbwHz / 1e3 << ',' << record.begin.toString(Qt::ISODate) << ','
               << record.end.toString(Qt::ISODate) << ',' << record.signalCount << ','
               << record.alarmCount << '\n';
    }
    emit logMessage(QStringLiteral("已导出录制列表：%1").arg(path));
}

void PlaybackPage::deleteSelected()
{
    const int row = selectedRow();
    if (row < 0 || row >= m_fileTable->rowCount()) return;
    const QString path = m_fileTable->item(row, 0)->data(Qt::UserRole).toString();
    for (int i = 0; i < m_records.size(); ++i) {
        if (m_records.at(i).path == path) {
            m_records.removeAt(i);
            break;
        }
    }
    refreshTable();
    emit logMessage(QStringLiteral("已从列表移除回放记录。"));
}

void PlaybackPage::showDetails()
{
    const int row = selectedRow();
    if (row < 0 || row >= m_fileTable->rowCount()) return;
    const QString path = m_fileTable->item(row, 0)->data(Qt::UserRole).toString();
    for (const auto& record : m_records) {
        if (record.path == path) {
            populateDetails(record);
            m_detailRecordPath = record.path;
            m_pages->setCurrentWidget(m_detailPage);
            break;
        }
    }
}

void PlaybackPage::openCurrentRow()
{
    const int row = selectedRow();
    if (row < 0 || row >= m_fileTable->rowCount()) return;
    const QString path = m_fileTable->item(row, 0)->data(Qt::UserRole).toString();
    emit replayRequested(path);
    showDetails();
}

void PlaybackPage::backToList()
{
    m_detailRecordPath.clear();
    m_pages->setCurrentWidget(m_listPage);
}

void PlaybackPage::filterRows(const QString&)
{
    refreshTable();
}

void PlaybackPage::populateDetails(const PlaybackRecord& record)
{
    m_detailRecordPath = record.path;
    const int resultCount = record.signalRows.size();
    m_detailTitle->setText(QStringLiteral("信号明细 - %1").arg(record.fileName));
    m_detailSummary->setText(QStringLiteral(
        "设备：%1    频段：%2 - %3 MHz    RBW：%4 kHz    文件：%5\n"
        "结果信号数：%6。可导出当前回放结果为 CSV 或 JSON。")
        .arg(record.source)
        .arg(record.startFrequencyHz / 1e6, 0, 'f', 3)
        .arg(record.endFrequencyHz / 1e6, 0, 'f', 3)
        .arg(record.rbwHz / 1e3, 0, 'f', 3)
        .arg(record.path)
        .arg(resultCount));
    m_signalTable->setRowCount(resultCount);
    for (int row = 0; row < resultCount; ++row) {
        const auto& signal = record.signalRows.at(row);
        m_signalTable->setItem(row, 0, item(signal.id));
        m_signalTable->setItem(row, 1, item(signal.centerFrequencyMHz));
        m_signalTable->setItem(row, 2, item(signal.bandwidthKHz));
        m_signalTable->setItem(row, 3, item(signal.type));
        m_signalTable->setItem(row, 4, item(signal.alarm));
        m_signalTable->setItem(row, 5, item(signal.lastSeen));
        m_signalTable->setItem(row, 6, item(signal.occurrenceCount));
        m_signalTable->item(row, 0)->setToolTip(signal.details);
    }
}

void PlaybackPage::exportCurrentSignals()
{
    const auto it = std::find_if(m_records.cbegin(), m_records.cend(),
        [this](const PlaybackRecord& record) { return record.path == m_detailRecordPath; });
    if (it == m_records.cend()) return;
    if (it->signalRows.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("暂无信号结果"),
                                 QStringLiteral("当前回放尚未产生可导出的检测结果。"));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出回放信号列表"),
        QStringLiteral("%1_signals.csv").arg(it->fileName),
        QStringLiteral("CSV files (*.csv);;JSON files (*.json)"));
    if (path.isEmpty()) return;
    QFile output(path);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("导出失败"), QStringLiteral("无法写入目标文件。"));
        return;
    }
    if (path.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)) {
        QJsonArray array;
        for (const auto& signal : it->signalRows) {
            array.append(QJsonObject{{QStringLiteral("id"), signal.id},
                {QStringLiteral("centerFrequencyMHz"), signal.centerFrequencyMHz},
                {QStringLiteral("bandwidthKHz"), signal.bandwidthKHz},
                {QStringLiteral("type"), signal.type}, {QStringLiteral("alarm"), signal.alarm},
                {QStringLiteral("lastSeen"), signal.lastSeen},
                {QStringLiteral("occurrenceCount"), signal.occurrenceCount},
                {QStringLiteral("details"), signal.details}});
        }
        output.write(QJsonDocument(array).toJson(QJsonDocument::Indented));
    } else {
        QTextStream stream(&output);
        stream << "id,center_frequency_mhz,bandwidth_khz,type,alarm,last_seen,occurrence_count\n";
        for (const auto& signal : it->signalRows) {
            stream << signal.id << ',' << signal.centerFrequencyMHz << ',' << signal.bandwidthKHz
                   << ',' << signal.type << ',' << signal.alarm << ',' << signal.lastSeen << ','
                   << signal.occurrenceCount << '\n';
        }
    }
    emit logMessage(QStringLiteral("已导出回放信号列表：%1").arg(path));
}

} // namespace scn::app
