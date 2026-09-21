#pragma once

#include <QDialog>
#include <QMouseEvent>

class QLabel;
class QPushButton;
class QWidget;

class HQMessageBox : public QDialog
{
    Q_OBJECT

public:
    static HQMessageBox* getInstance();
    void setTitle(const QString &title);
    void setMsgInfo(const QString &msg);

protected:
    // 重写鼠标事件：仅限标题栏拖动
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    explicit HQMessageBox(QWidget *parent = nullptr);
    ~HQMessageBox();

    HQMessageBox(const HQMessageBox&) = delete;
    HQMessageBox& operator=(const HQMessageBox&) = delete;

    void initUI();
    void initStyle();

private:
    QPoint m_dragPosition;
    bool m_isDragging;

    QWidget *m_headerWidget; // 标题栏容器
    QLabel *m_titleLabel;
    QPushButton *m_closeBtn;
    QLabel *m_msgLabel;
    QPushButton *m_yesBtn;
    QPushButton *m_noBtn;
};
