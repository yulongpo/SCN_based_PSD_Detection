#pragma once

#include <QDialog>
#include <QString>

class QLabel;
class QPushButton;
class QVBoxLayout;
class HQLineEdit;
class HQCComboBox;

/**
 * @brief 新增告警规则弹窗
 *
 * 无边框圆角弹窗，包含告警规则各字段的表单输入（规则名称、频率范围、
 * 信号类型、告警等级、带宽范围、备注），底部有"取消"和"保存规则"按钮。
 */
class WhiteRoleDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @brief 构造新增告警规则弹窗
     * @param parent 父控件
     */
    explicit WhiteRoleDialog(QWidget *parent = nullptr);

    /**
     * @brief 填充已有数据（编辑模式）
     * @param name      规则名称
     * @param freqStart 起始频率
     * @param freqEnd   截止频率
     * @param remark    备注
     */
    void setEditData(const QString &name, const QString &freqStart,
                     const QString &freqEnd, const QString &remark);

    /** @brief 获取规则名称 */
    QString ruleName() const;
    /** @brief 获取起始频率 */
    QString freqStart() const;
    /** @brief 获取截止频率 */
    QString freqEnd() const;
    /** @brief 获取备注 */
    QString remark() const;

    /**
     * @brief 设置弹窗标题
     * @param title 标题文字
     */
    void setDialogTitle(const QString &title);

protected:
    void paintEvent(QPaintEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    /** @brief 初始化标题栏 */
    void setupTitleBar();
    /** @brief 初始化表单内容 */
    void setupForm();
    /** @brief 初始化底部按钮 */
    void setupButtons();
    /** @brief 应用主题色 */
    void applyTheme();
    /** @brief DPR 缩放辅助 */
    int scaledPx(int designPx, int min = 0) const;

    QVBoxLayout *m_mainLayout = nullptr;  ///< 主布局
    QWidget     *m_titleBar   = nullptr;  ///< 标题栏容器
    QLabel      *m_titleLabel = nullptr;  ///< 标题文字
    QLabel      *m_accentLine = nullptr;  ///< 蓝色竖线
    QPushButton *m_closeBtn   = nullptr;  ///< 关闭按钮

    HQLineEdit  *m_nameEdit    = nullptr;  ///< 规则名称输入框
    HQLineEdit  *m_freqStart   = nullptr;  ///< 起始频率输入框
    HQLineEdit  *m_freqEnd     = nullptr;  ///< 截止频率输入框
    HQLineEdit  *m_remarkEdit  = nullptr;  ///< 备注输入框

    QPushButton *m_cancelBtn = nullptr;  ///< 取消按钮
    QPushButton *m_saveBtn   = nullptr;  ///< 保存规则按钮

    QPoint  m_dragPosition;
    bool    m_dragging    = false;
    bool    m_fixingPopup = false;

    enum { kRadius = 12 };
};
