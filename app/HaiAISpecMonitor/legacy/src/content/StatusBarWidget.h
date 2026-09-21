#pragma once

#include <QWidget>
#include <QLabel>
#include <QDateTime>

class ProgressBarWidget;

/**
 * @brief 底部状态栏控件
 *
 * 在内容面板底部显示系统状态信息、CPU/GPU/RAM 占用进度条，每项之间用竖线分隔，
 * 支持暗色/亮色主题切换，DPI 缩放自适应。
 *
 * 布局结构（固定高度 32px）：
 *   [CPU] [sep] [GPU] [sep] [RAM] [sep] [实时监测状态] [FILE 参数] [stretch] [时间] [设备]
 *
 * 颜色全部通过 theme_config.json 下的 statusBar 段配置。
 */
class StatusBarWidget : public QWidget
{
    Q_OBJECT

public:
    explicit StatusBarWidget(QWidget *parent = nullptr);

    /**
     * @brief 设置菜单的信息
     * @param fc 中心频率
     * @param bw 扫宽宽
     * @param rbw 带宽分辨率
     */
    void setMenuInfo(int64_t fc, int64_t bw, int64_t rbw);

    /**
     * @brief 设置设备名称
     * @param value 设备名称
     */
    void setDeviceValue(QString value);

    /**
     * @brief 设置中心频率/扫宽/带宽分辨率标签是否显示
     * @param visible[in] true 显示（测试数据 FILE 模式）；false 隐藏（真实设备模式）
     */
    void setMenuInfoVisible(bool visible);

    /**
     * @brief 显示实时检测累积状态。
     * @param stage DetectionStage 的整数值
     * @param accumulatedFrames 当前已累积帧数
     * @param minOutputFrames 快速检测最小帧数
     * @param requiredFrames 完整长窗帧数
     */
    void setDetectionProgress(int stage, int accumulatedFrames,
                              int minOutputFrames, int requiredFrames);

    /** @brief 清空并隐藏实时检测状态。 */
    void clearDetectionProgress();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    /// 从 ThemeManager 读取当前主题颜色并刷新样式
    void applyStyle();

    /// 创建文本标签（统一字体设置）
    QLabel *createLabel(const QString &objectName) const;

    /// 创建竖线分隔条（固定 1px 宽）
    QWidget *createSeparator() const;

    /// 创建一个通用进度条（统一字号、高度、宽度、背景色）
    ProgressBarWidget *createProgressBar(const QString &iconPath,
                                         const QString &label,
                                         const QString &fillColorKey) const;

    /// 更新内存提示框文字（格式化为合适的单位）
    static void updateMemoryTooltip(QWidget *widget, qint64 totalKB, qint64 usedKB);

private:
    QLabel             *m_detectionLabel            = nullptr;   ///< “实时监测”标签
    QLabel             *m_detectionValue            = nullptr;   ///< 当前检测阶段与累积进度
    QLabel             *m_alertLabel                = nullptr;   ///< 告警已启用（灰色标签）
    QLabel             *m_timeLabel                 = nullptr;   ///< "时间"（灰色标签）
    QLabel             *m_timeValue                 = nullptr;   ///< 时间
    QLabel             *m_deviceLabel               = nullptr;   ///< "采集设备"（灰色标签）
    QLabel             *m_deviceValue               = nullptr;   ///< 设备名称
    QLabel             *m_deviceCollectStatusLabel  = nullptr;   ///< "设备连接状态"（绿色数值）
    QLabel             *m_deviceCollectStatusValue  = nullptr;   ///< "设备连接状态"（绿色数值）
    QLabel             *m_networkCollectInfoLabel   = nullptr;   ///< "网络连接信息"（绿色数值）
    QLabel             *m_networkCollectInfoValue   = nullptr;   ///< "网络连接信息"（绿色数值）
    QLabel             *m_selfCheckStatusLabel      = nullptr;   ///< "自检状态"（绿色数值）
    QLabel             *m_selfCheckStatusValue      = nullptr;   ///< "自检状态"（绿色数值）
    QWidget            *m_sep1                      = nullptr;   ///< 第 1 条分隔竖线（CPU 后）
    QWidget            *m_sep2                      = nullptr;   ///< 第 2 条分隔竖线（GPU 后）
    QWidget            *m_sep3                      = nullptr;   ///< 实时监测状态前分隔线
    QWidget            *m_sep4                      = nullptr;   ///< 实时监测状态后分隔线
    ProgressBarWidget  *m_cpuProgress               = nullptr;   ///< CPU 占用进度条
    ProgressBarWidget  *m_gpuProgress               = nullptr;   ///< GPU 占用进度条
    ProgressBarWidget  *m_ramProgress               = nullptr;   ///< RAM 占用进度条

    // 内部测试显示
    QLabel             *m_fcLabel                   = nullptr;
    QLabel             *m_bwLabel                   = nullptr;
    QLabel             *m_rbwLabel                  = nullptr;

    // 内存数据缓存（用于 hover 提示框），单位 KB
    qint64 m_cpuMemoryTotalKB = 0;
    qint64 m_cpuMemoryUsedKB  = 0;
    qint64 m_gpuMemoryTotalKB = 0;
    qint64 m_gpuMemoryUsedKB  = 0;
};
