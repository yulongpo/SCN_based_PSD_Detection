#pragma once

#include <QDateTime>
#include <QVector>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QTableWidget;

namespace scn::app
{

struct PlaybackSignalRow
{
    QString id;
    // Detection measurements retain their algorithmic double precision;
    // presentation rounds to whole Hz in FrequencySpinBox::formatFrequency.
    double centerFrequencyHz = 0.0;
    double bandwidthHz = 0.0;
    QString type;
    QString alarm;
    QString lastSeen;
    QString occurrenceCount;
    QString details;
};

struct PlaybackRecord
{
    QString path;
    QString fileName;
    QString source;
    qint64 startFrequencyHz = 0;
    qint64 endFrequencyHz = 0;
    qint64 rbwHz = 0;
    QDateTime begin;
    QDateTime end;
    qint64 sizeBytes = 0;
    int signalCount = 0;
    int alarmCount = 0;
    QVector<PlaybackSignalRow> signalRows;
};

/**
 * @brief 原 ISA 的录制回放页面的 Qt6 兼容实现。
 *
 * 页面只管理回放记录和 UI 状态；实际频谱读取仍通过 FileSource 完成。
 */
class PlaybackPage final : public QWidget
{
    Q_OBJECT

public:
    explicit PlaybackPage(QWidget* parent = nullptr);

    void rememberFile(const QString& path, const QString& sourceName,
                      qint64 startFrequencyHz, qint64 endFrequencyHz,
                      qint64 resolutionBandwidthHz, int signalCount = 0,
                      int alarmCount = 0);
    void updateResultSignals(const QString& path, const QVector<PlaybackSignalRow>& rows);

signals:
    void replayRequested(const QString& path);
    void logMessage(const QString& message);

private slots:
    void importFile();
    void exportList();
    void exportCurrentSignals();
    void deleteSelected();
    void showDetails();
    void backToList();
    void filterRows(const QString& text);
    void openCurrentRow();

private:
    void buildUi();
    void buildListPage();
    void buildDetailPage();
    void appendRecord(const PlaybackRecord& record);
    void refreshTable();
    int selectedRow() const;
    QString formatDuration(const QDateTime& begin, const QDateTime& end) const;
    void populateDetails(const PlaybackRecord& record);

    QStackedWidget* m_pages = nullptr;
    QWidget* m_listPage = nullptr;
    QWidget* m_detailPage = nullptr;
    QLineEdit* m_filterEdit = nullptr;
    QLabel* m_recordCount = nullptr;
    QTableWidget* m_fileTable = nullptr;
    QTableWidget* m_signalTable = nullptr;
    QLabel* m_detailTitle = nullptr;
    QLabel* m_detailSummary = nullptr;
    QPushButton* m_exportSignalsButton = nullptr;
    QVector<PlaybackRecord> m_records;
    QString m_detailRecordPath;
};

} // namespace scn::app
