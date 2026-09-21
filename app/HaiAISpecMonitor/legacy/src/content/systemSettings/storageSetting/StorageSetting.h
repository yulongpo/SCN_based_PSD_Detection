#pragma once

#include <QWidget>
#include <QButtonGroup>
#include <map>

#include "BaseDef.h"

class QLabel;
class HQLineEdit;
class HQFileLineEdit;
class HQSwitch;

/**
 * @brief 存储策略设置页面
 *
 * 系统设置中"存储策略"标签页对应的内容面板，包含：
 * - 单文件最大大小（HQLineEdit，单位 MB）
 * - 存储溢出策略（自动覆盖/退出录制单选框）
 * - 存储溢出告警（开关）
 * - 默认存储路径配置（单个固定目录）
 */
class StorageSetting : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief 构造存储策略设置页面
     * @param parent 父控件
     */
    explicit StorageSetting(QWidget *parent = nullptr);

    /**
     * @brief 设置 HASM_SignalRepo 存储配置参数并展示在输入框，同时禁用尚未启用的存储策略控件
     * @param params[in] HASM_SignalRepo 存储配置参数表
     */
    void setSignalRepo(const std::map<std::string, std::string>& params);

signals:
    /**
     * @brief 发送参数下发请求
     * @param requestData[in] 请求数据
     */
    void signalRequest(haiq::GuiRequestData& requestData);

    /**
     * @brief 存储路径下发成功，通知上层同步更新（如回放页面的频谱数据存储路径）
     * @param newPath[in] 下发成功的新存储路径
     */
    void signalPathChanged(const QString& newPath);

public slots:
    /**
     * @brief 参数下发响应处理：仅当存储参数下发成功时才更新生效参数
     * @param responseData[in] 响应数据
     */
    void slotResponse(haiq::GuiResponseData& responseData);

    /**
     * @brief 设置默认存储路径输入框的可用状态（随监测运行状态联动）
     *
     * 监测运行期间禁止修改存储路径，监测停止或暂停时恢复可编辑。
     * @param flag[in] true-监测停止/暂停，路径可编辑；false-监测进行中，路径禁止编辑
     */
    void slotListDbRecord(bool flag);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    /**
     * @brief 初始化 UI 组件
     */
    void initUI();

    /**
     * @brief 存储路径编辑完成处理：路径变化时校验并下发参数
     */
    void onPathEditFinished();

private:
    int m_labelWidth                 = 0;       ///< 左侧标签固定宽度，用于内容对齐
    std::map<std::string, std::string> m_params; ///< HASM_SignalRepo 存储配置参数（当前生效）
    QString          m_pendingPath;              ///< 待确认的存储路径（下发成功后才更新到 m_params）

    // 子控件引用
    QLabel       *m_title          = nullptr;   ///< "存储策略"标题
    HQLineEdit   *m_maxSizeEdit    = nullptr;   ///< 单文件最大大小
    QButtonGroup *m_overflowGroup  = nullptr;   ///< 存储溢出策略按钮组
    HQSwitch     *m_overflowAlarmSwitch = nullptr; ///< 存储溢出告警开关
    HQFileLineEdit *m_pathEdit     = nullptr;   ///< 默认存储路径（单个固定目录）
};

