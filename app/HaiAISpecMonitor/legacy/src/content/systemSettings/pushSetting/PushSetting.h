#pragma once

#include <QWidget>
#include <QButtonGroup>

class QLabel;
class QVBoxLayout;
class HQLineEdit;
class HQCheckBox;
class HQCComboBox;

/**
 * @brief 推送设置页面
 *
 * 系统设置中"推送"标签页对应的内容面板，包含三个部分：
 * 1. 推送接口使能（UDP / REST API 复选框）
 * 2. UDP 配置（目标地址、端口号、推送内容）
 * 3. REST 配置（接口地址、接口名称、认证方式）
 * 各部分之间以虚线分隔。
 */
class PushSetting : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief 构造推送设置页面
     * @param parent 父控件
     */
    explicit PushSetting(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    /**
     * @brief 初始化 UI 组件
     */
    void initUI();

    /**
     * @brief 创建虚线分隔线
     * @return 虚线控件
     */
    QWidget* createSeparator();

    /** @brief 创建标签 */
    QLabel* createLabel(const QString &text, int fontSize, int fixedWidth = 0);

    // ============================================================
    // 第一部分：推送接口使能
    // ============================================================
    HQCheckBox *m_udpCheck   = nullptr;  ///< 启用 UDP 推送
    HQCheckBox *m_restCheck  = nullptr;  ///< 启用 REST API 推送

    // ============================================================
    // 第二部分：UDP 配置
    // ============================================================
    HQLineEdit  *m_udpAddrEdit   = nullptr;  ///< UDP 目标地址
    HQLineEdit  *m_udpPortEdit   = nullptr;  ///< UDP 端口号
    HQCheckBox  *m_udpSignal     = nullptr;  ///< UDP 推送内容 - 信号列表
    HQCheckBox  *m_udpAlert      = nullptr;  ///< UDP 推送内容 - 告警事件
    HQCheckBox  *m_udpSpectrum   = nullptr;  ///< UDP 推送内容 - 频谱数据

    // ============================================================
    // 第三部分：REST 配置
    // ============================================================
    HQLineEdit  *m_restUrlEdit   = nullptr;  ///< REST 接口地址
    HQCComboBox *m_restApiCbx    = nullptr;  ///< REST 接口名称
    QButtonGroup *m_authGroup    = nullptr;  ///< 认证方式按钮组

    /** 左侧标签固定宽度 */
    int m_labelWidth = 0;
};
