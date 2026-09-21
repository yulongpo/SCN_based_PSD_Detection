#pragma once

#include <QWidget>
#include <QRadioButton>
#include <QButtonGroup>

class QLabel;
class QPushButton;
class HQCComboBox;
class HQToolButton;

/**
 * @brief 显示设置界面
 *
 * 系统设置中"显示"标签页对应的内容面板，包含：
 * - 主题模式切换（浅色/深色/跟随系统）
 * - 频道/时频/标记配色（下拉框 + 取色板按钮）
 * - 恢复默认按钮
 */
class ShowSetting : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief 构造显示设置界面
     * @param parent 父控件
     */
    explicit ShowSetting(QWidget *parent = nullptr);

protected:
    /**
     * @brief 绘制圆角边框
     * @param event 绘制事件
     */
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

    /**
     * @brief 创建取色板按钮
     * @return 取色板按钮
     */
    QPushButton* createColorPickerBtn();

private:
    // 主题模式
    QLabel      *m_themeTitle   = nullptr;   ///< "主题模式"标题
    QButtonGroup *m_themeGroup  = nullptr;    ///< 主题单选按钮组

    // 配色设置
    QLabel      *m_colorTitle   = nullptr;   ///< "频道/时频/标记配色"标题
    HQCComboBox *m_spectrumCombo = nullptr;  ///< 频谱曲线颜色下拉框
    QPushButton *m_spectrumPicker = nullptr;  ///< 频谱曲线取色板按钮
    HQCComboBox *m_waterfallCombo = nullptr;  ///< 瀑布图色图下拉框
    HQCComboBox *m_markCombo     = nullptr;   ///< 标记颜色下拉框
    QPushButton *m_markPicker    = nullptr;   ///< 标记取色板按钮

    // 恢复默认
    HQToolButton *m_resetBtn    = nullptr;    ///< 恢复默认按钮
};
