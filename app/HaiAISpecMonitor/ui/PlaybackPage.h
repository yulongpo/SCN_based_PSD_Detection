#pragma once

#include <QDateTime>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QTableWidget;

namespace scn::app
{

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
                      double startFrequencyHz, double endFrequencyHz,
                      double resolutionBandwidthHz, int signalCount = 0,
                      int alarmCount = 0);

signals:
    void replayRequested(const QString& path);
    void logMessage(const QString& message);

private slots:
    void importFile();
    void exportList();
    void deleteSelected();
    void showDetails();
    void backToList();
    void filterRows(const QString& text);
    void openCurrentRow();

private:
    struct Record
    {
        QString path;
        QString fileName;
        QString source;
        double startFrequencyHz = 0.0;
        double endFrequencyHz = 0.0;
        double rbwHz = 0.0;
        QDateTime begin;
        QDateTime end;
        qint64 sizeBytes = 0;
        int signalCount = 0;
        int alarmCount = 0;
    };

    void buildUi();
    void buildListPage();
    void buildDetailPage();
    void appendRecord(const Record& record);
    void refreshTable();
    int selectedRow() const;
    QString formatDuration(const QDateTime& begin, const QDateTime& end) const;
    void populateDetails(const Record& record);

    QStackedWidget* m_pages = nullptr;
    QWidget* m_listPage = nullptr;
    QWidget* m_detailPage = nullptr;
    QLineEdit* m_filterEdit = nullptr;
    QLabel* m_recordCount = nullptr;
    QTableWidget* m_fileTable = nullptr;
    QTableWidget* m_signalTable = nullptr;
    QLabel* m_detailTitle = nullptr;
    QLabel* m_detailSummary = nullptr;
    QVector<Record> m_records;
};

} // namespace scn::app
