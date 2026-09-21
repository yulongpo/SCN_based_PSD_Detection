#pragma once

#include <QWidget>
#include <QStringList>
#include <QEvent>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QSet>

class QVBoxLayout;
class QLabel;
class HQCComboBox;
class HQLineEdit;
class HQCheckBox;
class QTableWidget;
class QPushButton;

class HQDetailCheckBoxDelegate;

/**
 * @brief 录制文件详情信号列表控件
 *
 * 顶部工具栏（排序下拉、频段输入、告警筛选、统计标签）+ 信号表格 + 底部翻页栏。
 * 带圆角边框，绘制风格与 RecordFileList 一致。
 */
class HQDetailList : public QWidget
{
    Q_OBJECT

public:
    /** @brief 列定义 */
    enum Column
    {
        ColCheckbox     = 0,   ///< 复选框
        ColID           = 1,   ///< ID
        ColFreq         = 2,   ///< 频点(MHz)
        ColBandwidth    = 3,   ///< 带宽(kHz)
        ColSignalType   = 4,   ///< 信号类型
        ColRecentCount  = 5,   ///< 最近出现次数
        ColTotalCount   = 6,   ///< 出现次数
        ColAvgCount     = 7,   ///< 平均出现次数
        ColAlertStatus  = 8,   ///< 告警状态
        ColOper         = 9,   ///< 操作
        ColumnCount     = 10   ///< 总列数
    };

    /**
     * @brief 构造详情信号列表控件
     * @param rowData 行数据
     * @param parent 父控件
     */
    explicit HQDetailList(const QStringList &rowData = QStringList(),
                          QWidget *parent = nullptr);

    /**
     * @brief 更新显示数据
     * @param rowData 行数据
     */
    void setData(const QStringList &rowData);

    /** @brief 添加一行数据 */
    void addRow(const QStringList &cells);
    /** @brief 设置全量信号数据（只填充当前页行，分页切换时自动刷新） */
    void setSignalData(const QVector<QStringList>& data);
    /** @brief 设置每条信号对应的时间线数据（与 setSignalData 的索引一一对应） */
    void setDetailTimeData(const QVector<QVector<QStringList>>& data);
    /** @brief 设置当前文件ID */
    void setCurrentFileId(qint64 fileId) { m_fileId = fileId; }
    /** @brief 原始信号数据，用于导出CSV */
    struct RawSignalItem {
        int64_t id;
        int64_t fc;
        int32_t bw;
        int32_t carry_type;
        int64_t recent_time;
        int32_t burst_num;
        int64_t avg_duration;
        QVector<QPair<int64_t, int64_t>> times; // <bgn_time, end_time>
    };
    /** @brief 设置原始信号明细数据，用于导出CSV */
    void setRawSignalData(const QVector<RawSignalItem>& items) { m_rawSignalItems = items; }
    /** @brief 判断当前是否有已加载的信号数据 */
    bool hasSignalData() const { return !m_rawSignalItems.isEmpty(); }
    /** @brief 获取当前详情页的文件ID */
    qint64 currentFileId() const { return m_fileId; }
    /** @brief 导出当前文件的信号明细和出现时间到CSV，返回是否写入了文件 */
    bool exportToCsv(const QString& dirPath) const;
    /** @brief 导出当前页信号的信号明细和出现时间到CSV，返回是否写入了文件 */
    bool exportCurPageToCsv(const QString& dirPath) const;
    /** @brief 导出全部信号的信号明细和出现时间到CSV，返回是否写入了文件 */
    bool exportAllToCsv(const QString& dirPath) const;
    /** @brief 清空所有行 */
    void clear();
    /** @brief 设置总记录数 */
    void setTotalCount(int count);
    /** @brief 设置已选记录数 */
    void setSelectedCount(int count);
    /** @brief 设置总页数 */
    void setTotalPage(int pages);
    /** @brief 设置当前页码 */
    void setCurrentPage(int page);
    /** @brief 获取当前页码 */
    int currentPage() const { return m_currentPage; }

signals:
    /** @brief 详情按钮点击，附带行数据 */
    void detailClicked(const QStringList &rowData);
    /** @brief 导出按钮点击 */
    void exportClicked();
    /** @brief 删除勾选条目按钮点击 */
    void deleteCheckedClicked();
    /** @brief 请求删除信号（文件ID, 信号ID列表） */
    void deleteSignalsRequested(qint64 fileId, const QVector<qint64>& signalIds);
    /** @brief 翻页 */
    void pageChanged(int page);
    /** @brief 每页条数改变 */
    void pageSizeChanged(int size);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    /** @brief 初始化 UI（仅在构造时调用一次） */
    void initUI();
    /** @brief 用当前 m_rowData 刷新文字 */
    void updateContent();
    /** @brief 创建底部翻页栏 */
    void createBottomBar();
    /** @brief 更新翻页按钮组 */
    void updatePageButtons();
    /** @brief 根据当前页码填充当前页行 */
    void refreshPage();
    /** @brief 更新全选复选框在表头中的位置 */
    void updateCheckAllPos();

    QVBoxLayout *m_layout     = nullptr;
    QStringList  m_rowData;

    // ---- 顶部工具栏控件 ----
    HQCComboBox *m_sortCombo  = nullptr;  ///< 排序下拉：按频点/按出现次数
    HQLineEdit  *m_freqStart  = nullptr;  ///< 起始频率输入
    HQLineEdit  *m_freqEnd    = nullptr;  ///< 结束频率输入
    HQCheckBox  *m_alertOnly  = nullptr;  ///< 仅显示告警复选框
    QLabel      *m_totalText  = nullptr;  ///< "总计：" 文字
    QLabel      *m_totalNum   = nullptr;  ///< 总计数字（加粗 14px）
    QLabel      *m_alertText  = nullptr;  ///< "告警：" 文字
    QLabel      *m_alertNum   = nullptr;  ///< 告警数字（加粗 14px，红色）

    // ---- 表格 ----
    int              m_cornerRadius = 0;   ///< 圆角半径（像素）
    QTableWidget    *m_table        = nullptr;  ///< 信号表格
    HQCheckBox      *m_checkAll     = nullptr;  ///< 全选复选框

    // ---- 底部翻页栏 ----
    QWidget         *m_bottomBar      = nullptr;  ///< 底部栏容器
    QLabel          *m_pageTotalLabel = nullptr;  ///< "共 x 条"
    QLabel          *m_selectedLabel  = nullptr;  ///< "已选择 x 条"
    QPushButton     *m_exportBtn      = nullptr;  ///< 导出按钮
    QPushButton     *m_deleteBtn      = nullptr;  ///< 删除勾选按钮
    QPushButton     *m_prevBtn        = nullptr;  ///< 上一页
    QPushButton     *m_nextBtn        = nullptr;  ///< 下一页
    QWidget         *m_pageBox        = nullptr;  ///< 页码按钮容器
    HQCComboBox     *m_pageSizeCombo  = nullptr;  ///< 每页条数下拉
    HQLineEdit      *m_jumpInput      = nullptr;  ///< 跳转页输入框

    int m_totalCount    = 0;   ///< 总记录数
    int m_currentPage   = 1;   ///< 当前页码
    int m_totalPage     = 1;   ///< 总页数
    int m_pageSize      = 10;  ///< 每页条数
    QVector<QStringList> m_signalData;  ///< 全量信号数据缓存
    QVector<QVector<QStringList>> m_detailTimeData;  ///< 每条信号对应的时间线数据
    QVector<RawSignalItem> m_rawSignalItems;  ///< 原始信号明细，用于导出CSV

    // ---- 信号详情面板（底部展开，初始隐藏） ----
    QWidget      *m_detailContainer = nullptr;  ///< 详情面板容器
    QLabel       *m_detailTitle     = nullptr;  ///< "信号详情" 标题
    QPushButton  *m_closeBtn        = nullptr;  ///< 关闭按钮
    QLabel       *m_idVal           = nullptr;  ///< ID 值（14px）
    QLabel       *m_freqVal         = nullptr;  ///< 频点值（14px）
    QLabel       *m_bwVal           = nullptr;  ///< 带宽值（14px）
    QTableWidget *m_detailTable     = nullptr;  ///< 时间线表格
    QVector<int> m_displayIndices;  ///< 当前页行号 → m_signalData/m_rawSignalItems 索引
    QSet<int> m_checkedRawIndices;  ///< 已勾选的原始数据索引（跨页持久化）
    qint64 m_fileId = 0;  ///< 当前文件ID
    QString m_fileName;   ///< 当前文件名（用于 CSV 导出前缀）
};


/**
 * @brief 表格列复选框代理
 *
 * 使用自定义 CheckStateRole(=Qt::UserRole+100) 存取状态，
 * 视觉与 HQCheckBox 完全一致。
 */
class HQDetailCheckBoxDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    using QStyledItemDelegate::QStyledItemDelegate;

    /** @brief 自定义数据角色，用于 QTableWidgetItem 存取复選状态 */
    static constexpr int CheckStateRole = Qt::UserRole + 100;

    /**
     * @brief 绘制复选框
     * @param painter 绘制器
     * @param option 样式选项
     * @param index 模型索引
     */
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;

    /**
     * @brief 处理鼠标点击切换复选框状态
     * @param event 事件
     * @param model 数据模型
     * @param option 样式选项
     * @param index 模型索引
     * @return true 表示事件已处理
     */
    bool editorEvent(QEvent *event, QAbstractItemModel *model,
                     const QStyleOptionViewItem &option,
                     const QModelIndex &index) override;
};

/** @brief 自定义数据角色，用于 HQDetailAlertDelegate 存取告警状态 */
static constexpr int AlertDetailModeRole = Qt::UserRole + 102;

/**
 * @brief 告警状态列（第 9 列）自绘委托
 *
 * 读取 AlertDetailModeRole：
 *   1 = 正常（绿色 #2CA25B）
 *   2 = 告警（红色 #E63E3E）
 * 绘制 [球形渐变 + 文字]，单元格不存显示文本。
 */
class HQDetailAlertDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    using QStyledItemDelegate::QStyledItemDelegate;

    /**
     * @brief 自绘告警状态列
     * @param painter 绘制器
     * @param option 样式选项
     * @param index 模型索引
     */
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
};
