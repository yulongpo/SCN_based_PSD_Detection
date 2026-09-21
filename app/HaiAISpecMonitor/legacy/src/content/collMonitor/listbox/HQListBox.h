#pragma once

#include <QWidget>
#include <QStringList>
#include <QEvent>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QPixmap>
#include <QSet>

#include "radioai/icd/HQSigMF.hpp"

class QTableWidget;

/// 自定义角色：告警等级列显示模式
static const int AlertLevelModeRole = Qt::UserRole + 101;

/**
 * @brief 信号列表控件，显示采集到的信号信息表格
 *
 * 基于 QTableWidget + QSS 样式实现：
 * - 暗色主题，奇偶行交替色
 * - 行选中模式，选中行背景偏蓝
 * - 第 7 列（告警等级）支持三种显示模式：
 *   1 = "正常"样式（绿色），2 = "一般"样式，3 = "严重"样式
 */
class HQListBox : public QWidget
{
    Q_OBJECT

public:
    /** @brief 列定义 */
    enum Column
    {
        ColID          = 0,  ///< ID
        ColFreq        = 1,  ///< 频率（MHz）
        ColBW          = 2,  ///< 带宽（kHz）
        ColSigType     = 3,  ///< 信号类型
        ColAlertLevel  = 4,  ///< 告警等级
        ColLastTime    = 5,  ///< 最近出现时间
        ColCount       = 6,  ///< 出现次数
        ColumnCount    = 7   ///< 总列数
    };

    /** @brief 告警等级显示模式 */
    enum AlertLevelMode
    {
        AlertNormal  = 1,   ///< "正常"样式（绿色）
        AlertGeneral = 2,   ///< "一般"样式
        AlertSevere  = 3    ///< "严重"样式
    };

    /**
     * @brief 构造函数
     * @param parent 父控件
     */
    explicit HQListBox(QWidget *parent = nullptr);

    /** @brief 添加一行数据 */
    void addRow(HQSigMF::DetectionObject* obj, int count = 1);


    /** @brief 清空所有行 */
    void clear();

    /** @brief 设置能够双击显示回放 */
    void setDbRecord(bool flag);

public slots:
    /**
     * @brief 根据时频图最后一帧的时间戳，删除过期行
     * @param lastFrameTimeMs 时频图最后一帧的时间戳（epoch 毫秒）
     *
     * 遍历所有行，如果某行的"最近出现时间"早于 lastFrameTimeMs，则删除该行。
     */
    void removeOldRows(int64_t lastFrameTimeMs);

    /**
     * @brief 根据 ID 定位行并滚动到表格视图中心位置（频谱图反向同步）
     * @param id 检测对象 ID
     *
     * 遍历所有行查找 ColID.UserRole 匹配的行，若找到则将该行滚动到视图中心。
     */
    void scrollToRowById(int64_t id);

signals:
    /**
     * @brief 通告信号变化
     * @param type 告警等级 0-普通 1-一般 2-严重
     * @param num 数量
     */
    void addSignalNum(int type, int num);

    /**
     * @brief 表格行双击信号（上层打开 RecordDialog 等操作）
     */
    void rowDoubleClicked();

    /**
     * @brief 行单击信号，携带标记框所需数据，供频谱图定位/高亮标记框
     * @param id         检测对象 ID
     * @param freqStart  起始频率（Hz）
     * @param freqStop   截止频率（Hz）
     * @param alarmLevel 告警等级（0=正常, 1=一般, 2=严重）
     */
    void rowClickedForMark(int64_t id, qint64 freqStart, qint64 freqStop, int alarmLevel);

protected:
    /**
     * @brief 事件过滤器，处理表格大小变化时更新圆角遮罩
     * @param obj 事件来源对象
     * @param event 事件对象
     * @return 是否已处理
     */
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    int              m_cornerRadius = 0;        ///< 圆角半径（像素）
    QTableWidget    *m_table        = nullptr;  ///< 表格控件
    bool             m_dbRecord     = false;    ///< 双击行是否能够查看回放
    QSet<int64_t>    m_processNum;              ///< 记录已处理的ID

    /**
     * @brief 单行增量重排：将第 srcRow 行向上冒泡，使其移动到满足排序规则的位置
     *
     * 排序规则：主排序告警等级从大到小（严重 > 一般 > 正常），次排序最近出现时间从大到小。
     * 与稳定排序语义一致：排序键相同的行保持原有相对顺序。
     * @param srcRow 需要重排的物理行号
     */
    void repositionRow(int srcRow);

    /**
     * @brief 判断第 r1 行的排序键是否大于第 r2 行
     * @param r1 行号 1
     * @param r2 行号 2
     * @return true：r1 应排在 r2 之前；false：r1 不应排在 r2 之前
     */
    bool rowKeyGreater(int r1, int r2) const;

    /**
     * @brief 交换表格中两行的所有列项
     * @param a 行号 a
     * @param b 行号 b
     */
    void swapTableRows(int a, int b);
};

/**
 * @brief 告警等级列（第 7 列）特殊绘制委托
 *
 * 第 7 列（告警等级）支持三种显示模式：
 * 1 = "正常"样式（绿色），2 = "一般"样式，3 = "严重"样式
 */
class HQRowDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    /**
     * @brief 初始化图标，按 DPR 预缩放到目标物理像素
     * @param dpr   设备像素比
     * @param scale 缩放系数
     */
    void initPixmaps(qreal dpr, double scale);

    /**
     * @brief 绘制委托，处理告警等级列特殊绘制
     * @param painter QPainter 对象
     * @param option  视图项选项
     * @param index   模型索引
     */
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;

private:
    /**
     * @brief 绘制告警等级单元格（badge 样式）
     * @param painter QPainter 对象
     * @param option  视图项选项
     * @param index   模型索引
     * @param mode    告警等级模式
     */
    void paintAlertCell(QPainter *painter, const QStyleOptionViewItem &option,
                        const QModelIndex &index, int mode) const;

    QPixmap  m_normalPix;                                     ///< 正常图标（绿色圆点，程序生成）
    QPixmap  m_generalPix;                                    ///< 一般告警图标
    QPixmap  m_severePix;                                     ///< 严重告警图标
};
