#include "StartupWindow.h"

#include <QApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPixmap>
#include <QTimer>
#include <QVBoxLayout>

namespace scn::app
{

namespace
{
QWidget* makeTaskRow(const QString& text, QWidget* parent, QLabel*& status)
{
    auto* row = new QFrame(parent);
    row->setObjectName(QStringLiteral("startupTaskRow"));
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(28, 0, 28, 0);

    auto* mark = new QLabel(QStringLiteral("○"), row);
    mark->setObjectName(QStringLiteral("startupTaskMark"));
    mark->setFixedWidth(28);
    auto* name = new QLabel(text, row);
    name->setObjectName(QStringLiteral("startupTaskName"));
    status = new QLabel(QStringLiteral("等待中"), row);
    status->setObjectName(QStringLiteral("startupTaskStatus"));
    status->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    layout->addWidget(mark);
    layout->addWidget(name, 1);
    layout->addWidget(status);
    return row;
}
}

StartupWindow::StartupWindow(QWidget* parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setMinimumSize(980, 600);
    resize(1000, 640);
    buildUi();

    m_timer = new QTimer(this);
    m_timer->setInterval(85);
    connect(m_timer, &QTimer::timeout, this, &StartupWindow::advanceStartup);
    m_timer->start();
}

void StartupWindow::buildUi()
{
    setStyleSheet(QStringLiteral(R"(
        QWidget { background: #061321; color: #d7e9fb; font-family: "Microsoft YaHei"; }
        QFrame#startupPanel { background: #071b30; border: 2px solid #1c7fc2; border-radius: 18px; }
        QLabel#startupLogo { color: #159cff; }
        QLabel#startupTitle { color: #f4f8ff; font-size: 27px; font-weight: 700; }
        QLabel#startupSubTitle { color: #7eafd4; font-size: 13px; }
        QLabel#startupPhase { color: #88c8f8; font-size: 12px; }
        QLabel#startupPercent { color: #139cff; font-size: 13px; font-weight: 700; }
        QProgressBar { background: #061527; border: 1px solid #1b4e7d; border-radius: 6px; height: 22px; text-align: right; color: #cceaff; }
        QProgressBar::chunk { background: #16b8e8; border-radius: 5px; }
        QFrame#startupTaskRow { background: #061525; border-bottom: 1px solid #15324d; min-height: 64px; }
        QLabel#startupTaskMark { color: #6f92b2; font-size: 27px; }
        QLabel#startupTaskName { color: #7e9fbe; font-size: 16px; }
        QLabel#startupTaskStatus { color: #6794ba; font-size: 15px; }
        QLabel#startupFooter { color: #7aa2c4; font-size: 9px; }
        QLabel#startupOnline { color: #13b5ff; font-size: 10px; }
    )"));

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(16, 16, 16, 16);
    auto* panel = new QFrame(this);
    panel->setObjectName(QStringLiteral("startupPanel"));
    outer->addWidget(panel);

    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(32, 24, 32, 18);
    layout->setSpacing(12);

    auto* titleRow = new QHBoxLayout;
    auto* logo = new QLabel(panel);
    logo->setObjectName(QStringLiteral("startupLogo"));
    logo->setFixedSize(72, 52);
    logo->setPixmap(QPixmap(QStringLiteral(":/title/title_log.png"))
        .scaled(72, 52, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    auto* title = new QLabel(QStringLiteral("智能频谱监测仪"), panel);
    title->setObjectName(QStringLiteral("startupTitle"));
    titleRow->addStretch();
    titleRow->addWidget(logo);
    titleRow->addSpacing(18);
    titleRow->addWidget(title);
    titleRow->addStretch();
    layout->addLayout(titleRow);

    auto* phaseRow = new QHBoxLayout;
    phaseRow->addStretch();
    m_phaseLabel = new QLabel(QStringLiteral("正在启动系统..."), panel);
    m_phaseLabel->setObjectName(QStringLiteral("startupPhase"));
    phaseRow->addWidget(m_phaseLabel);
    phaseRow->addStretch();
    layout->addLayout(phaseRow);

    auto* progressRow = new QHBoxLayout;
    m_progress = new QProgressBar(panel);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_percentLabel = new QLabel(QStringLiteral("0%"), panel);
    m_percentLabel->setObjectName(QStringLiteral("startupPercent"));
    m_percentLabel->setFixedWidth(42);
    progressRow->addWidget(m_progress, 1);
    progressRow->addWidget(m_percentLabel);
    layout->addLayout(progressRow);

    auto* tasks = new QFrame(panel);
    tasks->setObjectName(QStringLiteral("startupTaskList"));
    auto* taskLayout = new QVBoxLayout(tasks);
    taskLayout->setContentsMargins(0, 0, 0, 0);
    taskLayout->setSpacing(0);
    taskLayout->addWidget(makeTaskRow(QStringLiteral("初始化系统环境"), tasks, m_systemStatus));
    taskLayout->addWidget(makeTaskRow(QStringLiteral("初始化频谱采集功能"), tasks, m_sourceStatus));
    taskLayout->addWidget(makeTaskRow(QStringLiteral("初始化信号检测功能"), tasks, m_algorithmStatus));
    taskLayout->addWidget(makeTaskRow(QStringLiteral("初始化告警与数据管理功能"), tasks, m_storageStatus));
    taskLayout->addWidget(makeTaskRow(QStringLiteral("检查系统运行状态"), tasks, m_runtimeStatus));
    layout->addWidget(tasks, 1);

    auto* footer = new QHBoxLayout;
    auto* version = new QLabel(QStringLiteral("Version 1.1.0    |    Build 2026.09.21"), panel);
    version->setObjectName(QStringLiteral("startupFooter"));
    auto* online = new QLabel(QStringLiteral("● 正在检查运行环境"), panel);
    online->setObjectName(QStringLiteral("startupOnline"));
    footer->addWidget(version);
    footer->addStretch();
    footer->addWidget(online);
    layout->addLayout(footer);
}

void StartupWindow::advanceStartup()
{
    m_progressValue = qMin(100, m_progressValue + 4);
    m_progress->setValue(m_progressValue);
    m_percentLabel->setText(QStringLiteral("%1%").arg(m_progressValue));
    updateTaskRows();
    if (m_progressValue >= 100) {
        m_timer->stop();
        m_phaseLabel->setText(QStringLiteral("系统启动完成"));
        QTimer::singleShot(220, this, &StartupWindow::startupFinished);
    }
}

void StartupWindow::updateTaskRows()
{
    const QList<QPair<int, QLabel*>> tasks = {
        {16, m_systemStatus}, {30, m_sourceStatus}, {50, m_algorithmStatus},
        {70, m_storageStatus}, {90, m_runtimeStatus}
    };
    for (const auto& task : tasks) {
        auto* row = task.second->parentWidget();
        auto* mark = row ? row->findChild<QLabel*>(QStringLiteral("startupTaskMark")) : nullptr;
        if (m_progressValue >= task.first) {
            task.second->setText(QStringLiteral("已完成"));
            task.second->setStyleSheet(QStringLiteral("color:#159cff;font-size:15px;"));
            if (mark) {
                mark->setText(QStringLiteral("✓"));
                mark->setStyleSheet(QStringLiteral("color:#159cff;font-size:27px;"));
            }
        } else if (m_progressValue + 8 >= task.first) {
            task.second->setText(QStringLiteral("进行中"));
            task.second->setStyleSheet(QStringLiteral("color:#78b5e5;font-size:15px;"));
            if (mark) {
                mark->setText(QStringLiteral("◌"));
                mark->setStyleSheet(QStringLiteral("color:#6f92b2;font-size:27px;"));
            }
        } else {
            task.second->setText(QStringLiteral("等待中"));
            if (mark) mark->setText(QStringLiteral("○"));
        }
    }
    if (m_progressValue < 20) m_phaseLabel->setText(QStringLiteral("正在初始化运行环境..."));
    else if (m_progressValue < 40) m_phaseLabel->setText(QStringLiteral("正在初始化频谱采集功能..."));
    else if (m_progressValue < 60) m_phaseLabel->setText(QStringLiteral("正在初始化信号检测功能..."));
    else if (m_progressValue < 80) m_phaseLabel->setText(QStringLiteral("正在初始化告警与数据管理功能..."));
    else m_phaseLabel->setText(QStringLiteral("正在检查系统运行状态..."));
}

} // namespace scn::app
