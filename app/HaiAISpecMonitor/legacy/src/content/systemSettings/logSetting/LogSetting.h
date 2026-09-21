#pragma once

#include <QWidget>
#include <QButtonGroup>

class QLabel;
class QVBoxLayout;
class HQLineEdit;
class HQFileLineEdit;
class HQCheckBox;
class HQCComboBox;
class HQToolButton;

/**
 * @brief 日志设置页面
 *
 * 系统设置中"日志"标签页对应的内容面板，包含四个部分：
 * 1. 日志导出（导出范围、复选框、导出格式）
 * 2. 日志级别（记录级别下拉框）
 * 3. 日志文件存储位置（文件路径选择）
 * 4. 导出日志按钮
 * 各部分之间以虚线分隔。
 */
class LogSetting : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief 构造日志设置页面
     * @param parent 父控件
     */
    explicit LogSetting(QWidget *parent = nullptr);

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

    // ============================================================
    // 第一部分：日志导出
    // ============================================================
    QButtonGroup *m_exportRangeGroup = nullptr;  ///< 导出范围单选框组
    HQCheckBox   *m_logSys     = nullptr;        ///< 系统日志
    HQCheckBox   *m_logAlert   = nullptr;        ///< 告警日志
    HQCheckBox   *m_logResult  = nullptr;        ///< 检测结果日志
    HQCComboBox  *m_formatCbx  = nullptr;        ///< 导出格式下拉框

    // ============================================================
    // 第二部分：日志级别
    // ============================================================
    HQCComboBox  *m_levelCbx   = nullptr;        ///< 记录级别下拉框

    // ============================================================
    // 第三部分：日志文件存储位置
    // ============================================================
    HQFileLineEdit *m_pathEdit = nullptr;        ///< 当前日志路径

    // ============================================================
    // 第四部分：导出日志按钮
    // ============================================================
    HQToolButton *m_exportBtn  = nullptr;        ///< 导出日志按钮

    /** 左侧标签固定宽度 */
    int m_labelWidth = 0;
};
