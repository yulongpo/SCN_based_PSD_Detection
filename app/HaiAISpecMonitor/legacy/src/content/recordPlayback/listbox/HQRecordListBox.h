#pragma once

#include <QWidget>
#include <QStringList>
#include <QEvent>
#include <QPainter>
#include <QStyledItemDelegate>

#include "radioai/icd/SignalData.hpp"
#include "widgets/HQCheckBox.h"
#include <QSet>
class QTableWidget;
class QLabel;
class QPushButton;
class HQCComboBox;
class HQLineEdit;

class HQCheckBoxDelegate;

/**
 * @brief 信号列表控件，显示采集到的信号信息表格
 *
 * 基于 QTableWidget + QSS 样式实现：
 * - 暗色主题，奇偶行交替色
 * - 行级 hover 高亮
 * - 行选中模式，选中行背景偏蓝
 * - 第 7 列（告警等级）支持三种显示模式：
 *   1 = 不绘制，2 = 绘制"一般"样式，3 = 绘制"严重"样式
 */
class HQRecordListBox : public QWidget
{
    Q_OBJECT

public:
    /** @brief 列定义 */
    enum Column
    {
        ColCheckbox    = 0,   ///< 复选框
        ColIndex       = 1,   ///< 序号
        ColFileName    = 2,   ///< 文件名
        ColDevice      = 3,   ///< 采集设备
        ColStartF      = 4,   ///< 起始频率(MHz)
        ColEndF        = 5,   ///< 结束频率(MHz)
        ColRBW         = 6,   ///< RBW(Hz)
        ColStartT      = 7,   ///< 开始时间
        ColEndT        = 8,   ///< 结束时间
        ColTotalT      = 9,   ///< 总时长
        ColSize        = 10,  ///< 大小
        ColSignalNum   = 11,  ///< 信号数
        ColAlertNum    = 12,  ///< 告警数
        ColOper        = 13,  ///< 操作
        ColumnCount    = 14   ///< 总列数
    };

    /**
     * @brief 构造函数
     * @param parent 父控件
     */
    explicit HQRecordListBox(QWidget *parent = nullptr);

    /** @brief 添加一行数据 */
    void addRow(const QStringList &cells, const SpectrumFileInfo& info, qint64 fileId = 0);
    /** @brief 在最前面插入一行数据 */
    void insertRowAtFront(const QStringList &cells, const SpectrumFileInfo& info, qint64 fileId);

    /** @brief 更新或插入一行（根据 fileId 匹配已有行，存在则更新，否则追加） */
    void upsertRow(const QStringList &cells, const SpectrumFileInfo& info, qint64 fileId);

    /** @brief 获取指定行的文件ID */
    qint64 getRowFileId(int row) const;

    /** @brief 获取所有勾选行的文件ID */
    QVector<qint64> getCheckedFileIds() const;
    /** @brief 获取已勾选文件ID集合 */
    QSet<qint64> checkedFileIdSet() const { return m_checkedFileIds; }
    /** @brief 设置已勾选文件ID集合 */
    void setCheckedFileIds(const QSet<qint64>& ids) { m_checkedFileIds = ids; }
    /** @brief 从已勾选文件ID集合中移除指定文件ID并更新已选计数 */
    void removeCheckedFileIds(const QSet<qint64>& ids);
    /** @brief 查找文件ID在内部列表中的位置（0-based），未找到返回 -1 */
    int findFileIndex(qint64 fileId) const {
        for (int i = 0; i < m_allFileIds.size(); ++i)
            if (m_allFileIds[i] == fileId) return i;
        return -1;
    }

    /** @brief 获取所有行数据（每行12列+file_id，共13列） */
    QVector<QStringList> getAllRowData() const;

    /** @brief 清空所有行 */
    void clear();

    /** @brief 设置总记录数 */
    void setTotalCount(int count);
    /** @brief 设置已选记录数 */
    void setSelectedCount(int count);
    /** @brief 设置当前页码 */
    void setCurrentPage(int page);
    /** @brief 获取当前页码 */
    int currentPage() const { return m_currentPage; }
    /** @brief 获取每页条数 */
    int pageSize() const { return m_pageSize; }
    /** @brief 设置总页数 */
    void setTotalPage(int pages);
    /** @brief 打开回放界面播放这次实时的数据 */
    void openRecordDialog();

    /**
     * @brief 设置频谱数据存储路径
     * @param signalPath[in] 频谱数据存储路径
     */
    void setSignalPath(const QString& signalPath);

signals:
    /** @brief 导出按钮点击 */
    void exportClicked();
    /** @brief 删除清理按钮点击 */
    void deleteClicked();
    /** @brief 翻页 */
    void pageChanged(int page);
    /** @brief 每页条数改变 */
    void pageSizeChanged(int size);
    /** @brief 详情按钮点击，附带整行数据（ColIndex ~ ColAlertNum） */
    void detailClicked(const QStringList &rowData);

    /** @brief 行删除按钮点击 */
    void rowDeleteClicked(int row);

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
     * @brief 事件过滤器，处理 viewport 离开时清除 hover 状态
     */
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    /** @brief 更新全选复选框在表头中的位置 */
    void updateCheckAllPos();
    /** @brief 创建底部翻页栏 */
    void createBottomBar();
    /** @brief 更新翻页按钮组 */
    void updatePageButtons();

    /** @brief 根据当前页码和每页条数重建表格可见行 */
    void refreshPage();

private:
    int              m_cornerRadius = 0;        ///< 圆角半径（像素）
    QTableWidget    *m_table        = nullptr;  ///< 表格控件
    HQCheckBox      *m_checkAll     = nullptr;  ///< 全选复选框

    // ---- 底部翻页栏 ----
    QWidget         *m_bottomBar    = nullptr;  ///< 底部栏容器
    QLabel          *m_totalLabel   = nullptr;  ///< "共 x 条"
    QLabel          *m_selectedLabel = nullptr; ///< "已选择 x 条"
    QPushButton     *m_exportBtn    = nullptr;  ///< 导出按钮
    QPushButton     *m_deleteBtn    = nullptr;  ///< 删除清理按钮
    QPushButton     *m_prevBtn      = nullptr;  ///< 上一页
    QPushButton     *m_nextBtn      = nullptr;  ///< 下一页
    QWidget         *m_pageBox      = nullptr;  ///< 页码按钮容器
    HQCComboBox     *m_pageSizeCombo = nullptr; ///< 每页条数下拉
    HQLineEdit      *m_jumpInput     = nullptr; ///< 跳转页输入框

    int              m_totalCount    = 0;       ///< 总记录数
    int              m_selectedCount = 0;       ///< 已选记录数
    int              m_currentPage   = 1;       ///< 当前页码
    int              m_totalPage     = 1;       ///< 总页数
	int              m_pageSize      = 50;      ///< 每页条数
	
	SpectrumFileInfo m_realFileInfo;            ///< 实时文件信息
	// ---- 全量数据缓存（分页按需渲染） ----
    QVector<QStringList>       m_allRowData;    ///< 全量行数据
    QVector<SpectrumFileInfo>  m_allFileInfos;  ///< 全量文件信息（用于播放按钮等）
    QVector<qint64>            m_allFileIds;    ///< 全量文件ID
    QSet<qint64>               m_checkedFileIds; ///< 已勾选的文件ID（跨页持久化）

    QString          m_signalPath;              ///< 信号文件存储路径
};


/**
 * @brief 表格列复选框代理
 *
 * 使用自定义 CheckStateRole(=Qt::UserRole+100) 存取状态，
 * 视觉与 HQCheckBox 完全一致。
 */
class HQCheckBoxDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    using QStyledItemDelegate::QStyledItemDelegate;

    /** @brief 自定义数据角色，用于 QTableWidgetItem 存取复選状态 */
    static constexpr int CheckStateRole = Qt::UserRole + 100;

    /**
     * @brief 绘制复选框（先调用基类绘制，再叠加自定义复选框）
     */
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;

    /**
     * @brief 处理鼠标点击切换复选框状态
     */
    bool editorEvent(QEvent *event, QAbstractItemModel *model,
                     const QStyleOptionViewItem &option,
                     const QModelIndex &index) override;
};