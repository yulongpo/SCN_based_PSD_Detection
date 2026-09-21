#include "HQSourceSwitchDialog.h"

#include "comm/FontManager.h"
#include "comm/ScreenScale.h"
#include "widgets/HQCComboBox.h"

#include <QHBoxLayout>
#include <QLabel>

// ============================================================================
// 构造
// ============================================================================

HQSourceSwitchDialog::HQSourceSwitchDialog(QWidget *parent)
    : HQGeneralDialog(QStringLiteral("信号源切换"), parent)
{
    setObjectName("HQSourceSwitchDialog");

    // 固定弹窗尺寸
    setDialogSize(scaledPx(420, 420), scaledPx(200, 200));

    // 内容区：文件源标签 + 下拉框（水平布局）
    auto *contentWidget = new QWidget(this);
    auto *hBoxLayout = new QHBoxLayout(contentWidget);
    hBoxLayout->setContentsMargins(scaledPx(20, 10), scaledPx(4, 2),
                                   scaledPx(20, 10), scaledPx(4, 2));
    hBoxLayout->setSpacing(scaledPx(16, 8));

    // 左侧"文件源"标签
    auto *label = new QLabel(QStringLiteral("信号源"), contentWidget);
    QFont labelFont = FontManager::instance().font(scaledPx(13, 9), QFont::Normal);
    label->setFont(labelFont);
    label->setStyleSheet("color: rgba(255,255,255,0.8); background: transparent;");
    label->setFixedHeight(scaledPx(36, 24));
    hBoxLayout->addWidget(label);

    // 右侧文件源下拉框
    m_sourceCombo = new HQCComboBox(contentWidget);
    m_sources = QVector<QString>()
            << QStringLiteral("BB60C")
            << QStringLiteral("MR60C")
            << QStringLiteral("HarogicSAN90")
            << QStringLiteral("FILE");
    for (const QString &source : m_sources)
        m_sourceCombo->addItem(source);
    m_sourceCombo->setCurrentIndex(0);   // 默认选中第一个文件源
    hBoxLayout->addWidget(m_sourceCombo, 1);

    hBoxLayout->addStretch();

    // 添加到基类内容区布局（位于标题栏与底部按钮之间）
    contentLayout()->addWidget(contentWidget);
}

// ============================================================================
// 公共接口
// ============================================================================

QString HQSourceSwitchDialog::selectedSource() const
{
    return m_sourceCombo ? m_sourceCombo->currentText() : QString();
}

void HQSourceSwitchDialog::setCurrentSource(const QString &source)
{
    if (!m_sourceCombo)
        return;
    for (int i = 0; i < m_sources.size(); ++i)
    {
        if (m_sources.at(i) == source)
        {
            m_sourceCombo->setCurrentIndex(i);
            return;
        }
    }
}
