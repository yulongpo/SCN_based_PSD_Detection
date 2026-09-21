#pragma once

#include <QWidget>
#include <QPixmap>
#include <QVboxLayout>
#include <memory>
#include "CollMonitor.h"
#include "menu/CollMonitorMenu.h"
#include "StdAfx.h"

class HQSigMF;

/**
 * @brief 通用圆角面板控件（采集监测面板）
 *
 * 绘制一个带圆角边框的 widget，圆角半径 24px，边框颜色 rgba(30,30,40,1)，
 */
class MonitorIndex : public QWidget
{
    Q_OBJECT

public:
    explicit MonitorIndex(QWidget *parent = nullptr);

    /** @brief 返回 CollMonitorMenu 的最小内容宽度（逻辑像素） */
    int minimumContentWidth() const;

    /**
     * @brief 设置参数
     * @param params 参数列表
     */
    void setParams(ParamTable& params);

    /**
     * @brief 更新参数响应
     * @param responseData 响应数据
     */
    void slotResponse(haiq::GuiResponseData& responseData);

    /**
     * @brief 向 CollMonitor 的 HQSigMF 处理队列添加数据
     * @param data HQSigMF 数据共享指针
     */
    void enqueueIcdData(QSharedPointer<HQSigMF> data);

signals:
    /**
     * @brief 发送请求
     * @param requestData 请求数据
     */
    void signalRequest(haiq::GuiRequestData& requestData);

    /**
     * @brief 表格行双击信号（上层打开 RecordDialog 等操作）
     */
    void signalRowDoubleClicked();

    /**
     * @brief 通知自身参数信息
     * @param fc 中心频率
     * @param bw 扫宽
     * @param rbw 带宽分辨率
     */
    void signalMenuInfo(int64_t fc, int64_t bw, int64_t rbw);

    /**
     * @brief 转发监测运行状态（是否允许列出数据库记录），用于控制存储路径输入框等联动控件
     * @param flag[in] true-监测停止/暂停；false-监测进行中
     */
    void signalListDbRecord(bool flag);

    /**
     * @brief 转发文件源切换通知
     * @param source 文件源名称（BB60C/MR60C/HarogicSAN90/FILE）
     */
    void signalSourceSwitch(const QString &source);

    /** @brief 转发实时检测累积进度到底部状态栏。 */
    void detectionProgressChanged(int stage, int accumulatedFrames,
                                  int minOutputFrames, int requiredFrames);

    /** @brief 转发实时检测状态清除通知。 */
    void detectionProgressCleared();

private:
    void initUI();

private:
    CollMonitorMenu *m_collMonitorMenu = nullptr;   ///< 采集监测菜单控件
    CollMonitor     *m_collMonitor     = nullptr;   ///< 采集监测对象
};
