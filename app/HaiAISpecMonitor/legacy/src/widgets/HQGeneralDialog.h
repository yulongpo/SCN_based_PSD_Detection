#pragma once

#include <QDialog>
#include <QString>

class QLabel;
class QPushButton;
class QVBoxLayout;

/**
 * @brief 通用弹窗基类
 *
 * 无边框圆角弹窗，内置标题栏（蓝色竖线、标题文字、关闭按钮）和底部
 * "确定 / 取消"两个按钮，支持主题适配、标题栏拖拽、屏幕居中弹出。
 *
 * 子类继承后只需：
 *  - 调用 setDialogSize() 设置弹窗固定尺寸
 *  - 通过 contentLayout() 或 m_mainLayout 添加业务内容
 *  - 需要时调用 setOkButtonText() / setButtonSize() 定制按钮
 *  - 覆盖 onOkClicked() 定制"确定"按钮的点击行为（默认 accept()）
 */
class HQGeneralDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @brief 构造通用弹窗
     * @param title  弹窗标题
     * @param parent 父控件
     */
    explicit HQGeneralDialog(const QString &title, QWidget *parent = nullptr);
    ~HQGeneralDialog() override;

    /**
     * @brief 设置弹窗标题
     * @param title[in] 标题文字
     */
    void setDialogTitle(const QString &title);

    /**
     * @brief 设置弹窗固定尺寸
     * @param width[in]  宽度（已缩放的实际像素）
     * @param height[in] 高度（已缩放的实际像素）
     */
    void setDialogSize(int width, int height);

    /**
     * @brief 设置"确定"按钮文字
     * @param text[in] 按钮文字
     */
    void setOkButtonText(const QString &text);

    /**
     * @brief 设置底部按钮尺寸
     * @param width[in]  按钮宽度（已缩放的实际像素）
     * @param height[in] 按钮高度（已缩放的实际像素）
     */
    void setButtonSize(int width, int height);

    /**
     * @brief 获取内容区布局，子类通过其添加业务内容
     *
     * 内容区布局固定位于标题栏与底部按钮之间，子类添加的内容不会覆盖按钮区。
     * @return 内容区垂直布局指针
     */
    QVBoxLayout *contentLayout() const;

    /**
     * @brief 获取"确定"按钮
     * @return 确定按钮指针
     */
    QPushButton *okButton() const;

    /**
     * @brief 获取"取消"按钮
     * @return 取消按钮指针
     */
    QPushButton *cancelButton() const;

protected:
    /** @brief 重写绘制事件，绘制圆角背景与边框 */
    void paintEvent(QPaintEvent *event) override;
    /** @brief 重写事件过滤，实现标题栏拖拽 */
    bool eventFilter(QObject *watched, QEvent *event) override;
    /** @brief 重写显示事件，首次显示时居中弹出 */
    void showEvent(QShowEvent *event) override;

    /**
     * @brief "确定"按钮点击回调，子类可覆盖以定制行为
     */
    virtual void onOkClicked();

    /**
     * @brief DPR 缩放辅助
     * @param designPx[in] 设计稿像素
     * @param min[in]      最小像素值
     * @return 缩放后的逻辑像素
     */
    int scaledPx(int designPx, int min = 0) const;

    QVBoxLayout *m_mainLayout    = nullptr;  ///< 主布局（标题栏 + 内容区 + 按钮区）
    QVBoxLayout *m_contentLayout = nullptr;  ///< 内容区布局（位于标题栏与按钮区之间）
    QWidget     *m_titleBar   = nullptr;  ///< 标题栏容器
    QLabel      *m_titleLabel = nullptr;  ///< 标题文字
    QLabel      *m_accentLine = nullptr;  ///< 蓝色竖线
    QPushButton *m_closeBtn   = nullptr;  ///< 关闭按钮

    QPushButton *m_okBtn     = nullptr;  ///< 确定按钮
    QPushButton *m_cancelBtn = nullptr;  ///< 取消按钮

    QPoint  m_dragPosition;         ///< 拖拽起始位置
    bool    m_dragging = false;     ///< 是否正在拖拽标题栏
    bool    m_centered  = false;    ///< 是否已执行过首次居中

    enum { kRadius = 12 };  ///< 弹窗圆角半径

private:
    /** @brief 初始化标题栏 */
    void setupTitleBar(const QString &title);
    /** @brief 初始化底部按钮 */
    void setupButtons();
    /** @brief 应用主题色 */
    void applyTheme();
};
