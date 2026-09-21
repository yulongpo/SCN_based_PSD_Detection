#pragma once

#include <QWidget>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QPixmap>
#include "radioai/icd/SignalData.hpp"
#include "com/common/SignalWhitelist.hpp"

class QTableWidget;
class QLabel;
class QPushButton;

/**
 * @brief 告警等级列绘制委托
 *
 * 基于 AlertLevelModeRole 数据绘制图标 + "一般" / "严重" badge，
 * 不依赖单元格文字内容，避免重复显示。
 */
class AlertLevelDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    using QStyledItemDelegate::QStyledItemDelegate;

    /**
     * @brief 初始化告警图标（按 DPR 预缩放）
     * @param dpr   设备像素比
     * @param scale 缩放系数
     */
    /** 自定义角色：告警等级列显示模式 */
    static const int AlertLvlRole = Qt::UserRole + 101;

    void initPixmaps(qreal dpr, double scale);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;

private:
    /**
     * @brief 绘制告警等级 badge
     * @param painter QPainter
     * @param option  视图项选项
     * @param index   模型索引
     * @param mode    告警等级模式（2=一般，3=严重）
     */
    void paintBadge(QPainter *painter, const QStyleOptionViewItem &option,
                    const QModelIndex &index, int mode) const;

    QPixmap m_generalPix;
    QPixmap m_severePix;
};

/**
 * @brief 告警规则设置页面
 *
 * 系统设置中"告警规则"标签页对应的内容面板，包含：
 * - 顶部标题栏（"告警规则" + "新增规则"按钮）
 * - 12 列规则列表（支持告警等级自定义绘制、启用开关、编辑删除操作）
 */
class AlertSetting : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief 构造告警规则设置页面
     * @param parent 父控件
     */
    explicit AlertSetting(QWidget *parent = nullptr);

    /**
     * @brief 设置信号告警规则列表
     * @param rules 告警规则列表
     */
    void setSignalAlarmRules(SignalAlarmRules rules);

    /**
     * @brief 设置信号白名单列表
     * @param whiteLists 信号白名单列表
     */
    void setSignalWhiteLists(SignalWhitelists whiteLists);

signals:
    /**
     * @brief 告警规则操作请求
     * @param request 告警规则操作请求信息
     */
    void alarmRuleOpReq(const SignalAlarmRuleOpReq &request);

    /**
     * @brief 白名单操作请求
     * @param request 白名单操作请求信息
     */
    void whiteListsOpReq(const SignalWhitelistOpReq &request);

public slots:
    /**
     * @brief 告警规则操作响应
     * @param response 告警规则操作响应信息
     */
    void alarmRuleOpResp(const SignalAlarmRuleOpResp &response);

    /**
     * @brief 白名单操作响应
     * @param response 白名单操作响应信息
     */
    void whiteListsOpResp(const SignalWhitelistOpResp &response);

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    /**
     * @brief 初始化 UI 组件
     */
    void initUI();

    static const int RuleIdRole = Qt::UserRole + 1;

    /**
     * @brief 向表格添加一行数据
     * @param cells          各列数据
     * @param ruleId         后端规则ID
     * @param enabled        启用状态
     * @param alertLevelMode 告警等级模式（2=一般，3=严重）
     */
    void addRow(const QStringList &cells, int64_t ruleId = 0, bool enabled = true, int alertLevelMode = 0);

    /**
     * @brief 为指定行创建操作按钮（编辑/删除）
     * @param row 行索引
     */
    void setupOperationRow(int row);

    /**
     * @brief 向白名单表格添加一行数据
     * @param cells   各列数据
     * @param whiteId 后端白名单ID
     * @param enabled 启用状态
     */
    void addWhiteRow(const QStringList &cells, int64_t whiteId = 0, bool enabled = true);

    /**
     * @brief 为白名单指定行创建操作按钮（编辑/删除）
     * @param row 行索引
     */
    void setupWhiteOperationRow(int row);

    int m_pendingRefresh = 0;  ///< 等待刷新响应计数（告警规则 + 白名单）

    QTableWidget *m_table = nullptr;        ///< 告警规则表格
    QTableWidget *m_whiteTable = nullptr;   ///< 白名单规则表格
    QPushButton  *m_addBtn = nullptr;       ///< 新增规则按钮
    QPushButton  *m_whiteAddBtn = nullptr;  ///< 白名单新增规则按钮

public:
    /** 列索引 */
    enum AlertColumn {
        ColIndex     = 0,   ///< 序号
        ColName      = 1,   ///< 规则名称
        ColFreqStart = 2,   ///< 起始频率(MHz)
        ColFreqEnd   = 3,   ///< 截止频率(MHz)
        ColSigType   = 4,   ///< 信号类型
        ColAlertLevel= 5,   ///< 告警等级
        ColBWMax     = 6,   ///< 信号最大带宽(kHz)
        ColBWMin     = 7,   ///< 信号最小带宽(kHz)
        ColRemark    = 8,   ///< 备注
        ColEnabled   = 9,   ///< 启用状态
        ColOperation = 10,  ///< 操作（编辑/删除）
        ColumnCount  = 11
    };

    enum WhiteColumn {
        WhiteColIndex     = 0,   ///< 序号
        WhiteColName      = 1,   ///< 规则名称
        WhiteColFreqStart = 2,   ///< 起始频率(MHz)
        WhiteColFreqEnd   = 3,   ///< 截止频率(MHz)
        WhiteColRemark    = 4,   ///< 备注
        WhiteColEnabled   = 5,   ///< 启用状态
        WhiteColOperation = 6,  ///< 操作（编辑/删除）
        WhiteColumnCount  = 7
    };
};
