#pragma once

#include <QWidget>

class QLabel;
class QVBoxLayout;
class HQLineEdit;
class HQToolButton;

/**
 * @brief 帮助/关于页面
 *
 * 系统设置中"帮助"标签页对应的内容面板，包含四个部分：
 * 1. 版本信息（软件版本 + 构建日期）
 * 2. 硬件版本（设备型号、硬件版本、序列号）
 * 3. 授权信息（设备型号 + 已授权状态、授权有效期、授权功能）
 * 4. 操作按钮（检查更新、授权管理）
 * 各部分之间以虚线分隔。
 */
class HelpSetting : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief 构造帮助页面
     * @param parent 父控件
     */
    explicit HelpSetting(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    /**
     * @brief 初始化 UI 组件
     */
    void initUI();

    /**
     * @brief 创建虚线分隔线
     */
    QWidget* createSeparator();

    /** @brief 创建标签 */
    QLabel* createLabel(const QString &text, int fontSize, int fixedWidth = 0);

    /** @brief 创建"已授权"状态标签（含图标 + 绿色文字） */
    QWidget* createAuthStatusWidget();

    // ============================================================
    // 第一部分：版本信息
    // ============================================================
    HQLineEdit *m_softVerEdit = nullptr;  ///< 软件版本

    // ============================================================
    // 第二部分：硬件版本
    // ============================================================
    HQLineEdit *m_deviceModelEdit = nullptr;  ///< 设备型号
    HQLineEdit *m_hardVerEdit     = nullptr;  ///< 硬件版本
    HQLineEdit *m_serialEdit      = nullptr;  ///< 序列号

    // ============================================================
    // 第三部分：授权信息
    // ============================================================
    HQLineEdit *m_authDateEdit  = nullptr;  ///< 授权有效期
    HQLineEdit *m_authFuncEdit  = nullptr;  ///< 授权功能

    // ============================================================
    // 第四部分：按钮
    // ============================================================
    HQToolButton *m_updateBtn = nullptr;  ///< 检查更新按钮
    HQToolButton *m_licenseBtn = nullptr; ///< 授权管理按钮

    /** 左侧标签固定宽度 */
    int m_labelWidth = 0;
};
