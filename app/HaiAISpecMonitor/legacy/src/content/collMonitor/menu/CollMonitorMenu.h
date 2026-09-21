#pragma once

#include <QWidget>
#include <QPixmap>
#include <QVboxLayout>
#include <QGridLayout>
#include <QLabel>
#include "widgets/HQFileLineEdit.h"
#include "widgets/HQLineEdit.h"
#include "widgets/HQSpinBox.h"
#include "HQButton.h"
#include "widgets/HQCComboBox.h"
#include "StdAfx.h"
#include "widgets/ToastWidget.h"
#include "HQFreqLineEdit.h"
#include "innerTest/HQSourceSwitchDialog.h"

struct InnerTestData
{
    int64_t m_fc;
    int64_t m_span;
    int64_t m_rbw;
    int64_t m_refLevel;
};

/**
 * @brief 通用圆角面板控件（采集监测面板）
 *
 * 绘制一个带圆角边框的 widget，圆角半径 24px，边框颜色 rgba(30,30,40,1)，
 */
class CollMonitorMenu : public QWidget
{
    Q_OBJECT

public:
    explicit CollMonitorMenu(QWidget *parent = nullptr);

    /**
     * @brief 设置参数
     * @param params 参数列表
     */
    void setParams(ParamTable& params);

signals:
    /**
     * @brief 发送请求
     * @param requestData 请求数据
     */
    void signalRequest(haiq::GuiRequestData& requestData);

    /**
     * @brief 通知自身参数
     * @param fc 中心频率
     * @param bw 带宽
     * @param refLevel 参考电平
     */
    void signalParams(int64_t fc, int64_t bw, int64_t refLevel);

    /**
     * @brief 通知是否刷新实时数据
     * @param flag true:刷新数据
     */
    void signalIsUpdateData(bool flag);

    /**
     * @brief 通知清空当前数据状态
     */
    void signalClearData();

    /**
     * @brief 通知列表框双击回放
     * @param flag true 允许双击回放、
     */
    void signalListDbRecord(bool flag);

    /**
     * @brief 通知自身参数信息
     * @param fc 中心频率
     * @param bw 扫宽
     * @param rbw 带宽分辨率
     */
    void signalMenuInfo(int64_t fc, int64_t bw, int64_t rbw);

    /**
     * @brief 通知文件源已切换
     * @param source 文件源名称（BB60C/MR60C/HarogicSAN90/FILE）
     */
    void signalSourceSwitch(const QString &source);

public slots:
    /**
     * @brief 更新参数响应
     * @param responseData 响应数据
     */
    void slotResponse(haiq::GuiResponseData& responseData);

    /**
     * @brief 通告信号变化
     * @param type 告警等级 0-普通 1-一般 2-严重
     * @param num 数量
     */
    void addSignalNum(int type, int num);

    /**
     * @brief 文件路径发生改变
     * @param filePath 文件路径
     */
    void filePathChanged(const QString& filePath);

protected:
    void paintEvent(QPaintEvent *event) override;

    /**
     * @brief 事件过滤：监听开始按钮右键双击
     * @param watched[in] 被监视对象
     * @param event[in]   事件
     * @return true 表示事件已处理
     */
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void initUI();

    void initConnections();

    /**
     * @brief 更新采集设备的UI
     */
    void updateCollectMonitorUI();

    /**
     * @brief 设置菜单参数
     * @param params 参数
     */
    void setMenuParams(const std::map<std::string, std::string>& params);

    /**
     * @brief 开始按钮点击
     */
    void onStartBtn_clicked();

    /**
     * @brief 暂停按钮点击
     */
    void onPauseBtn_clicked();

    /**
     * @brief 开始按钮右键双击：打开文件源切换弹窗
     */
    void onStartBtnDoubleClicked();

    /**
     * @brief 设置输入框等是否可用
     * @param enable true:可用
     */
    void setContentEnable(bool enable);

    /**
     * @brief 提取出文件名称中包含的中心频率，带宽等信息
     * @param filePath 文件全路径
     * @param fc 中心频率
     * @param bw 扫宽
     * @param rbw 带宽分辨率
     * @param refLevel 参考电平
     */
    void parseFilePath(const std::string& filePath, int64_t& fc, int64_t& bw, int64_t& rbw, int64_t& refLevel);

    /**
     * @brief 设置界面状态
     * @param flag true 测试状态
     */
    void setTestData(bool flag);
private:
    QVector<QLabel*>     m_collectMonitorLabels;// 用于更新样式
    QLabel *m_filePathLabel;                    // 文件路径标题 (用于控制显示隐藏)
    HQFileLineEdit *m_filePathEdit;             // 文件路径输入框
    QLabel* m_frequencyLabel;                   // 中心频率标题 (用于控制显示隐藏)
    QLabel* m_scanWidthLabel;                   // 扫宽标题 (用于控制显示隐藏)
    QLabel* m_rbwLabel;                         // rbw标题 (用于控制显示隐藏)
    HQFreqLineEdit *m_frequencyEdit;            // 中心频率输入框
    HQFreqLineEdit *m_scanWidthEdit;            // 扫宽输入框
    HQFreqLineEdit *m_rbwEdit;                  // rbw输入框
    HQFreqLineEdit *m_startFreqEdit;            // 起始频率输入框
    HQFreqLineEdit *m_endFreqEdit;              // 终止频率输入框
    HQSpinBox *m_refLevelSpin;                  // 参考电平
    HQCComboBox *m_refLevelSpinUnit;            // 参考电平单位
    HQButton *m_startBtn;                       // 开始检测按钮
    HQButton *m_pauseBtn;                       // 暂停检测按钮

    QLabel  *m_criticalAlertNumLabel;           // 严重警告数字标签
    QLabel  *m_generalAlarmNumLabel;            // 一般警告数字标签
    QLabel  *m_signalTotalNumLabel;             // 信号总数数字标签
    QWidget *m_panel;                           // 左侧面板容器（grid + 按钮 + 三个统计标记）

    bool m_startBtnStatus;                      // 开始按钮当前状态
    bool m_pauseBtnStatus;                      // 暂停按钮当前状态
    bool m_updatingFreq;                        // 频率四联动互斥标志，防止递归更新
    bool m_isTestData;                          // 是否是测试数据 true 是，如果是测试数据，菜单项不能编辑并且中心频率，扫宽，带宽分辨率移动到底部状态栏显示
    bool m_isDbClickSource;                     // 是否可以右键双击打开修改文件源提示框 true 是

    std::map<std::string, std::string> m_params;// 旧的参数
    std::map<std::string, std::string> m_newParams;// 新的参数

    InnerTestData m_innerTestData;              // 记录上一次操作的数据用于切换文件源恢复
};
