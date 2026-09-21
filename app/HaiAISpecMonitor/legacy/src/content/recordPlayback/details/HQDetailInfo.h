#pragma once

#include <QWidget>
#include <QStringList>

class QVBoxLayout;
class QLabel;

/**
 * @brief 录制文件详情信息控件
 *
 * 顶部工具栏（返回/播放）+ 文件信息详情（文件名标题 + Grid 布局字段展示）。
 * 结构固定（一次创建），setData 只替换文字以提升性能。
 * 带圆角边框，绘制风格与 RecordFileList 一致。
 */
class HQDetailInfo : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief 构造详情信息控件
     * @param rowData 行数据（ColIndex ~ ColAlertNum 共 12 列）
     * @param parent 父控件
     */
    explicit HQDetailInfo(const QStringList &rowData = QStringList(),
                          QWidget *parent = nullptr);

    /**
     * @brief 更新显示数据（只替换文字，不重建控件树）
     * @param rowData 行数据（ColIndex ~ ColAlertNum 共 12 列）
     */
    void setData(const QStringList &rowData);

signals:
    /** @brief 返回按钮点击 */
    void backClicked();

protected:
    /** @brief 绘制圆角边框及工具栏分割线 */
    void paintEvent(QPaintEvent *event) override;

private:
    /** @brief 初始化 UI（仅在构造时调用一次） */
    void initUI();
    /** @brief 用当前 m_rowData 刷新所有文字（setData 时调用） */
    void updateContent();

    QVBoxLayout *m_layout = nullptr;        ///< 内部布局
    QStringList  m_rowData;                  ///< 当前行数据

    // ---- 预创建控件指针（只更新文字，不重建） ----
    QWidget *m_toolbar     = nullptr;        ///< 工具栏（用于 paintEvent 绘制分割线定位）
    QLabel  *m_titleLabel  = nullptr;        ///< 文件名标题
    QLabel  *m_gridValues[2][4] = {};        ///< Grid 值标签 [行][列]
};
