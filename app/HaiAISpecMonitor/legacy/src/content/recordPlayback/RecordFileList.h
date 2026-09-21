#pragma once

#include <QWidget>

#include "content/collMonitor/listbox/HQListBox.h"
#include "listbox/HQRecordListBox.h"

class QVBoxLayout;

/**
 * @brief 录制文件列表控件
 *
 * 带圆角边框的可滚动文件列表面板，边框颜色从主题配置 CollMonitor.borderColor 读取。
 */
class RecordFileList : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief 构造录制文件列表
     * @param parent 父控件
     */
    explicit RecordFileList(QWidget *parent = nullptr);

signals:
    /** @brief 详情按钮点击，转发自内部 HQRecordListBox */
    void detailClicked(const QStringList &rowData);

    /** @brief 底部删除清理按钮点击，转发自内部 HQRecordListBox */
    void deleteClicked();

    /** @brief 行删除按钮点击，转发自内部 HQRecordListBox */
    void rowDeleteClicked(int row);

    /** @brief 导出按钮点击，转发自内部 HQRecordListBox */
    void exportClicked();

    /**
     * @brief 信号列表查询
     * @param fileId 文件ID
     */
    void dialogSignalDetailSearch(int64_t fileId);

    /**
     * @brief 信号列表
     * @param resp 信号列表
     */
    void dialogSignalDetail(const SignalDetailPerFileQueryResp& resp);

protected:
    /**
     * @brief 绘制圆角边框
     * @param event 绘制事件
     */
    void paintEvent(QPaintEvent *event) override;

private:
    void initUI();

public:
    /** @brief 获取所有勾选行的文件ID，委托给 m_listBox */
    QVector<qint64> getCheckedFileIds() const { return m_listBox->getCheckedFileIds(); }
    /** @brief 获取已勾选文件ID集合 */
    QSet<qint64> checkedFileIdSet() const { return m_listBox->checkedFileIdSet(); }
    /** @brief 设置已勾选文件ID集合 */
    void setCheckedFileIds(const QSet<qint64>& ids) { m_listBox->setCheckedFileIds(ids); }
    /** @brief 从已勾选文件ID集合中移除指定文件ID并更新已选计数 */
    void removeCheckedFileIds(const QSet<qint64>& ids) { m_listBox->removeCheckedFileIds(ids); }
    /** @brief 查找文件ID在内部列表中的位置（0-based），未找到返回 -1 */
    int findFileIndex(qint64 fileId) const { return m_listBox->findFileIndex(fileId); }
    /** @brief 获取指定行的文件ID */
    qint64 getRowFileId(int row) const { return m_listBox->getRowFileId(row); }
    /** @brief 添加一行 */
    void addRow(const QStringList &cells, const SpectrumFileInfo& info, qint64 fileId = 0) { m_listBox->addRow(cells, info, fileId); }
    /** @brief 在最前面插入一行数据 */
    void insertRowAtFront(const QStringList &cells, const SpectrumFileInfo& info, qint64 fileId) { m_listBox->insertRowAtFront(cells, info, fileId); }
    /** @brief 更新或插入一行 */
    void upsertRow(const QStringList &cells, const SpectrumFileInfo& info, qint64 fileId) { m_listBox->upsertRow(cells, info, fileId); }
    /** @brief 清空所有行 */
    void clearList() { m_listBox->clear(); }
    /** @brief 获取所有行数据（每行13列：index,file_name,device,startF,endF,rbw,startT,endT,totalT,size,signalNum,alertNum,fileId） */
    QVector<QStringList> getAllRowData() const { return m_listBox->getAllRowData(); }
    /** @brief 设置总记录数 */
    void setTotalCount(int count) { m_listBox->setTotalCount(count); }
    /** @brief 设置总页数 */
    void setTotalPage(int pages) { m_listBox->setTotalPage(pages); }
    /** @brief 设置当前页码 */
    void setCurrentPage(int page) { m_listBox->setCurrentPage(page); }
    /** @brief 获取当前页码 */
    int currentPage() const { return m_listBox->currentPage(); }
    /** @brief 获取每页条数 */
    int pageSize() const { return m_listBox->pageSize(); }
    /** @brief 打开回放界面播放这次实时的数据 */
    void openRecordDialog();

    /**
     * @brief 设置频谱数据存储路径
     * @param signalPath[in] 频谱数据存储路径
     */
    void setSignalPath(const QString& signalPath);

private:
    QVBoxLayout *m_layout = nullptr;  ///< 内部布局

    HQRecordListBox *m_listBox = nullptr;
};
