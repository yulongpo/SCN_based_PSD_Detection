#pragma once

#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTableWidget>
#include <QWidget>

class QStackedWidget;

namespace scn::app
{

/**
 * @brief 原 ISA 系统设置页面的 Qt6 兼容实现。
 *
 * 设置页只维护界面配置和操作日志；告警、白名单、存储等业务接口后续
 * 可以接入 application 层，不把旧组件依赖带回算法热路径。
 */
class SettingsPage final : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsPage(QWidget* parent = nullptr);

signals:
    void logMessage(const QString& message);

private slots:
    void selectPage(int index);
    void addRule();
    void removeRule();
    void applyStorage();
    void clearLog();
    void exportLog();

private:
    void buildUi();
    QWidget* buildDisplayPage();
    QWidget* buildStoragePage();
    QWidget* buildRulePage();
    QWidget* buildPushPage();
    QWidget* buildLogPage();
    QWidget* buildHelpPage();
    void appendLog(const QString& message);

    QStackedWidget* m_stack = nullptr;
    QPlainTextEdit* m_logEdit = nullptr;
    QLineEdit* m_storagePath = nullptr;
    QTableWidget* m_ruleTable = nullptr;
};

} // namespace scn::app
