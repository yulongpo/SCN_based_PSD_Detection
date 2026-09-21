#pragma once

#include <QWidget>

class QLabel;
class QProgressBar;
class QTimer;

namespace scn::app
{

/**
 * @brief ISA 风格启动页。
 *
 * 这里只负责展示应用初始化阶段，不承载检测算法或数据源逻辑。
 */
class StartupWindow final : public QWidget
{
    Q_OBJECT

public:
    explicit StartupWindow(QWidget* parent = nullptr);

signals:
    void startupFinished();

private slots:
    void advanceStartup();

private:
    void buildUi();
    void updateTaskRows();

    QProgressBar* m_progress = nullptr;
    QLabel* m_phaseLabel = nullptr;
    QLabel* m_percentLabel = nullptr;
    QLabel* m_systemStatus = nullptr;
    QLabel* m_sourceStatus = nullptr;
    QLabel* m_algorithmStatus = nullptr;
    QLabel* m_storageStatus = nullptr;
    QLabel* m_runtimeStatus = nullptr;
    QTimer* m_timer = nullptr;
    int m_progressValue = 0;
};

} // namespace scn::app
