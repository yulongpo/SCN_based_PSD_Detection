#include "SettingsPage.h"

#include <QAbstractItemView>
#include <QButtonGroup>
#include <QCheckBox>
#include <QDateTime>
#include <QFileDialog>
#include <QFile>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextStream>
#include <QToolButton>
#include <QVBoxLayout>

namespace scn::app
{

namespace
{
QPushButton* navButton(const QString& text, QWidget* parent)
{
    auto* button = new QPushButton(text, parent);
    button->setCheckable(true);
    button->setFixedSize(82, 32);
    button->setObjectName(QStringLiteral("settingsNavButton"));
    return button;
}

QPushButton* actionButton(const QString& text, QWidget* parent)
{
    auto* button = new QPushButton(text, parent);
    button->setObjectName(QStringLiteral("pageToolButton"));
    button->setMinimumHeight(34);
    return button;
}

QWidget* formContainer(QWidget* parent)
{
    auto* widget = new QWidget(parent);
    widget->setObjectName(QStringLiteral("settingsCard"));
    return widget;
}
}

SettingsPage::SettingsPage(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
}

void SettingsPage::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);

    auto* nav = new QHBoxLayout;
    const QStringList names = {QStringLiteral("显示"), QStringLiteral("存储"),
                               QStringLiteral("规则"), QStringLiteral("推送"),
                               QStringLiteral("日志"), QStringLiteral("帮助")};
    auto* group = new QButtonGroup(this);
    group->setExclusive(true);
    for (int i = 0; i < names.size(); ++i) {
        auto* button = navButton(names.at(i), this);
        group->addButton(button, i);
        nav->addWidget(button);
    }
    nav->addStretch();
    root->addLayout(nav);

    m_stack = new QStackedWidget(this);
    m_stack->addWidget(buildDisplayPage());
    m_stack->addWidget(buildStoragePage());
    m_stack->addWidget(buildRulePage());
    m_stack->addWidget(buildPushPage());
    m_stack->addWidget(buildLogPage());
    m_stack->addWidget(buildHelpPage());
    root->addWidget(m_stack, 1);

    group->button(0)->setChecked(true);
    connect(group, &QButtonGroup::idClicked, this, &SettingsPage::selectPage);
}

QWidget* SettingsPage::buildDisplayPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* card = formContainer(page);
    auto* form = new QFormLayout(card);
    form->setContentsMargins(22, 22, 22, 22);
    form->setVerticalSpacing(16);
    form->addRow(new QLabel(QStringLiteral("显示设置"), card));
    auto* grid = new QCheckBox(QStringLiteral("显示频谱网格线"), card);
    grid->setChecked(true);
    form->addRow(QStringLiteral("网格"), grid);
    auto* marker = new QCheckBox(QStringLiteral("显示信号标记和频率提示"), card);
    marker->setChecked(true);
    form->addRow(QStringLiteral("标记"), marker);
    auto* maxHold = new QCheckBox(QStringLiteral("启用最大保持谱"), card);
    form->addRow(QStringLiteral("最大保持"), maxHold);
    auto* waterfall = new QCheckBox(QStringLiteral("显示瀑布图"), card);
    waterfall->setChecked(true);
    form->addRow(QStringLiteral("瀑布图"), waterfall);
    auto* rows = new QSpinBox(card);
    rows->setRange(50, 2000);
    rows->setValue(100);
    form->addRow(QStringLiteral("瀑布行数"), rows);
    auto* apply = actionButton(QStringLiteral("应用显示设置"), card);
    form->addRow(QString(), apply);
    connect(apply, &QPushButton::clicked, this, [this] {
        appendLog(QStringLiteral("显示设置已应用。"));
    });
    layout->addWidget(card, 0, Qt::AlignTop);
    layout->addStretch();
    return page;
}

QWidget* SettingsPage::buildStoragePage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* card = formContainer(page);
    auto* form = new QFormLayout(card);
    form->setContentsMargins(22, 22, 22, 22);
    m_storagePath = new QLineEdit(QStringLiteral("./data/spectrum"), card);
    auto* browse = actionButton(QStringLiteral("选择目录"), card);
    auto* pathRow = new QHBoxLayout;
    pathRow->addWidget(m_storagePath, 1);
    pathRow->addWidget(browse);
    form->addRow(QStringLiteral("频谱存储路径"), pathRow);
    auto* autoSave = new QCheckBox(QStringLiteral("监测时自动保存频谱"), card);
    autoSave->setChecked(true);
    form->addRow(QStringLiteral("自动存储"), autoSave);
    auto* retention = new QSpinBox(card);
    retention->setRange(1, 3650);
    retention->setValue(30);
    retention->setSuffix(QStringLiteral(" 天"));
    form->addRow(QStringLiteral("保留周期"), retention);
    auto* apply = actionButton(QStringLiteral("应用存储策略"), card);
    form->addRow(QString(), apply);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getExistingDirectory(this, QStringLiteral("选择频谱存储目录"));
        if (!path.isEmpty()) m_storagePath->setText(path);
    });
    connect(apply, &QPushButton::clicked, this, &SettingsPage::applyStorage);
    layout->addWidget(card, 0, Qt::AlignTop);
    layout->addStretch();
    return page;
}

QWidget* SettingsPage::buildRulePage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* toolbar = new QHBoxLayout;
    toolbar->addWidget(new QLabel(QStringLiteral("告警规则"), page));
    toolbar->addStretch();
    auto* add = actionButton(QStringLiteral("新增规则"), page);
    auto* remove = actionButton(QStringLiteral("删除规则"), page);
    toolbar->addWidget(add);
    toolbar->addWidget(remove);
    layout->addLayout(toolbar);
    m_ruleTable = new QTableWidget(0, 7, page);
    m_ruleTable->setObjectName(QStringLiteral("isaTable"));
    m_ruleTable->setHorizontalHeaderLabels({QStringLiteral("规则名称"), QStringLiteral("频率范围"),
        QStringLiteral("带宽范围"), QStringLiteral("信号类型"), QStringLiteral("告警等级"),
        QStringLiteral("启用"), QStringLiteral("备注")});
    m_ruleTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_ruleTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_ruleTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_ruleTable->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    layout->addWidget(m_ruleTable, 1);
    connect(add, &QPushButton::clicked, this, &SettingsPage::addRule);
    connect(remove, &QPushButton::clicked, this, &SettingsPage::removeRule);
    return page;
}

QWidget* SettingsPage::buildPushPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* card = formContainer(page);
    auto* form = new QFormLayout(card);
    form->setContentsMargins(22, 22, 22, 22);
    auto* enable = new QCheckBox(QStringLiteral("启用告警推送"), card);
    form->addRow(QStringLiteral("推送状态"), enable);
    auto* endpoint = new QLineEdit(QStringLiteral("http://127.0.0.1:8080/api/alarm"), card);
    form->addRow(QStringLiteral("服务地址"), endpoint);
    auto* token = new QLineEdit(card);
    token->setEchoMode(QLineEdit::Password);
    form->addRow(QStringLiteral("访问令牌"), token);
    auto* test = actionButton(QStringLiteral("发送测试消息"), card);
    form->addRow(QString(), test);
    connect(test, &QPushButton::clicked, this, [this] {
        appendLog(QStringLiteral("已发送一条推送测试消息（接口占位）。"));
    });
    layout->addWidget(card, 0, Qt::AlignTop);
    layout->addStretch();
    return page;
}

QWidget* SettingsPage::buildLogPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    m_logEdit = new QPlainTextEdit(page);
    m_logEdit->setReadOnly(true);
    m_logEdit->setPlaceholderText(QStringLiteral("系统操作日志将在这里显示。"));
    layout->addWidget(m_logEdit, 1);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    auto* clear = actionButton(QStringLiteral("清空日志"), page);
    auto* exportButton = actionButton(QStringLiteral("导出日志"), page);
    buttons->addWidget(clear);
    buttons->addWidget(exportButton);
    layout->addLayout(buttons);
    connect(clear, &QPushButton::clicked, this, &SettingsPage::clearLog);
    connect(exportButton, &QPushButton::clicked, this, &SettingsPage::exportLog);
    return page;
}

QWidget* SettingsPage::buildHelpPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* text = new QPlainTextEdit(page);
    text->setReadOnly(true);
    text->setPlainText(QStringLiteral(
        "智能频谱监测仪\n\n"
        "采集监测：选择 BB60C、Harogic 或 FILE 数据源，配置频段后开始监测。\n"
        "录制回放：导入文本/CSV/ASC 或 float32 二进制频谱文件，双击记录可进入信号明细。\n"
        "系统设置：管理显示、存储、告警规则、推送和日志。\n\n"
        "当前重构版本：Qt 6.11.1 / VS 2026。检测引擎保留接口，未加载旧 Flow、Component 或 HQSigMF 级联。"));
    layout->addWidget(text);
    return page;
}

void SettingsPage::selectPage(int index)
{
    m_stack->setCurrentIndex(index);
}

void SettingsPage::addRule()
{
    const int row = m_ruleTable->rowCount();
    m_ruleTable->insertRow(row);
    const QStringList values = {QStringLiteral("新规则"), QStringLiteral("全频段"),
        QStringLiteral("0 - 10000 kHz"), QStringLiteral("未知"), QStringLiteral("一般"),
        QStringLiteral("是"), QStringLiteral("待配置")};
    for (int column = 0; column < values.size(); ++column)
        m_ruleTable->setItem(row, column, new QTableWidgetItem(values.at(column)));
    appendLog(QStringLiteral("新增一条告警规则。"));
}

void SettingsPage::removeRule()
{
    const int row = m_ruleTable->currentRow();
    if (row < 0) return;
    m_ruleTable->removeRow(row);
    appendLog(QStringLiteral("删除一条告警规则。"));
}

void SettingsPage::applyStorage()
{
    appendLog(QStringLiteral("存储策略已应用：%1").arg(m_storagePath->text()));
}

void SettingsPage::appendLog(const QString& message)
{
    if (m_logEdit) {
        m_logEdit->appendPlainText(QStringLiteral("[%1] %2")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss")), message));
    }
    emit logMessage(message);
}

void SettingsPage::clearLog()
{
    if (m_logEdit) m_logEdit->clear();
}

void SettingsPage::exportLog()
{
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出日志"),
        QStringLiteral("hai_ai_spec_monitor.log"), QStringLiteral("Log files (*.log *.txt)"));
    if (path.isEmpty() || !m_logEdit) return;
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream stream(&file);
        stream << m_logEdit->toPlainText();
        emit logMessage(QStringLiteral("已导出系统日志：%1").arg(path));
    }
}

} // namespace scn::app
