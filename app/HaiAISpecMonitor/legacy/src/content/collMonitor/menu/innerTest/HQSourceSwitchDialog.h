#pragma once

#include "widgets/HQGeneralDialog.h"

#include <QString>
#include <QVector>

class HQCComboBox;

/**
 * @brief 文件源切换弹窗
 *
 * 继承自通用弹窗 HQGeneralDialog，内容为一行"文件源"标签与下拉框，
 * 下拉框可选项：BB60C、MR60C、HarogicSAN90、FILE，供用户切换文件源。
 * 标题栏、底部"确定/取消"按钮由基类提供。
 */
class HQSourceSwitchDialog : public HQGeneralDialog
{
    Q_OBJECT

public:
    /**
     * @brief 构造文件源切换弹窗
     * @param parent 父控件
     */
    explicit HQSourceSwitchDialog(QWidget *parent = nullptr);

    /**
     * @brief 获取当前选择的文件源
     * @return 文件源名称
     */
    QString selectedSource() const;

    /**
     * @brief 设置当前选择的文件源
     * @param source[in] 文件源名称
     */
    void setCurrentSource(const QString &source);

private:
    HQCComboBox *m_sourceCombo = nullptr;  ///< 文件源下拉框
    QVector<QString> m_sources;            ///< 文件源选项列表（与下拉框项一一对应）
};
