#pragma once

#include "widgets/HQGeneralDialog.h"

class QButtonGroup;
class QRadioButton;

/**
 * @brief 导出数据弹窗
 *
 * 继承自通用弹窗 HQGeneralDialog，标题栏、主题、拖拽、居中及底部
 * "确定 / 取消"按钮均由基类提供。本类在内容区放置三个单选按钮选择
 * 导出方式（导出选中数据、导出当前页数据、导出全部数据），三选一，
 * 并将"确定"按钮文字改为"导出"。点击"导出"后对选中的项发射对应
 * 信号并关闭弹窗。
 */
class HQExportDialog : public HQGeneralDialog
{
    Q_OBJECT

public:
    /**
     * @brief 构造导出数据弹窗
     * @param parent 父控件
     */
    explicit HQExportDialog(QWidget *parent = nullptr);

signals:
    /** @brief 用户选择"导出选中数据" */
    void exportSelectedClicked();
    /** @brief 用户选择"导出当前页数据" */
    void exportCurrentPageClicked();
    /** @brief 用户选择"导出全部数据" */
    void exportAllClicked();

protected:
    /** @brief 重写确定按钮点击：按当前选中的导出方式发射对应信号并关闭弹窗 */
    void onOkClicked() override;

private:
    /** @brief 初始化三个导出选项单选按钮 */
    void setupOptions();

    QButtonGroup *m_exportGroup      = nullptr;  ///< 导出方式互斥分组（单选）
    QRadioButton *m_exportSelectedRb = nullptr;  ///< 导出选中数据单选按钮
    QRadioButton *m_exportCurrentRb  = nullptr;  ///< 导出当前页数据单选按钮
    QRadioButton *m_exportAllRb      = nullptr;  ///< 导出全部数据单选按钮
};
