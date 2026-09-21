#pragma once

#include <QWidget>

class QLineEdit;
class QPushButton;
class QPropertyAnimation;

/**
 * @brief 带文件夹浏览按钮的路径输入框
 *
 * 视觉风格与 HQLineEdit 完全一致，将右侧单位文字替换为文件夹图标按钮，
 * 点击按钮弹出目录选择对话框，选中后路径自动填入输入框。
 */
class HQFileLineEdit : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal emphasisProgress READ emphasisProgress WRITE setEmphasisProgress)

public:
    /**
     * @brief 构造路径输入框
     * @param parent 父控件
     */
    explicit HQFileLineEdit(QWidget *parent = nullptr);
    ~HQFileLineEdit() override;

    /**
     * @brief 获取/设置路径文本
     */
    QString text() const;
    void setText(const QString &text);

    /**
     * @brief 获取/设置占位提示文字
     */
    QString placeholderText() const;
    void setPlaceholderText(const QString &text);

    /**
     * @brief 设置是否选择文件（默认 false=选择目录）
     * @param selectFile true=弹出文件选择对话框，false=弹出目录选择对话框
     *
     * 当外部设置了文件路径后，可调用此方法切换到文件选择模式，
     * 点击浏览按钮时将弹出文件选择对话框而非目录选择对话框。
     */
    void setSelectFile(bool selectFile);

    /** @brief 当前是否为文件选择模式 */
    bool isSelectFile() const { return m_selectFile; }

    QSize sizeHint() const override;

signals:
    /** 文本内容改变信号 */
    void textChanged(const QString &text);
    /** 编辑完成信号（失去焦点或回车触发） */
    void editingFinished();

protected:
    void enterEvent(QEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

    /**
     * @brief 状态变更事件：启用状态变化时同步更新鼠标指针样式
     * @param event[in] 事件对象
     */
    void changeEvent(QEvent *event) override;

private slots:
    /** 响应主题切换 */
    void applyThemeAppearance(bool night);
    /** 点击浏览按钮打开目录选择对话框 */
    void onBrowse();

private:
    qreal emphasisProgress() const { return m_emphasisProgress; }
    void setEmphasisProgress(qreal progress);

    QColor themeColor(const QString &key, const QColor &fallback = QColor()) const;
    void animateEmphasisTo(qreal value);
    qreal targetEmphasis() const;
    void refreshMetrics();
    int scaledPx(int designPx, int min = 0) const;

    QLineEdit           *m_lineEdit;            ///< 内部输入框
    QPushButton         *m_browseBtn;           ///< 文件夹浏览按钮
    QPropertyAnimation  *m_emphasisAnimation;   ///< 强调状态过渡动画
    qreal                m_emphasisProgress;    ///< 当前强调动画进度（0=base，1=focus）
    bool                 m_hovered;             ///< 鼠标是否悬停
    bool                 m_focused;             ///< 内部输入框是否持有焦点
    bool                 m_selectFile;          ///< true=选择文件，false=选择目录（默认）
    bool                 m_cursorOverridden = false; ///< 是否已设置全局禁止光标覆盖
};
