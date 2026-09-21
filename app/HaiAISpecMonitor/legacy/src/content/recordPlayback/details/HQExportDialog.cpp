#include "HQExportDialog.h"

#include "comm/FontManager.h"
#include "widgets/ToastWidget.h"

#include <QButtonGroup>
#include <QRadioButton>
#include <QVBoxLayout>

// ============================================================================
// 构造
// ============================================================================

HQExportDialog::HQExportDialog(QWidget *parent)
    : HQGeneralDialog(QStringLiteral("导出数据"), parent)
{
    setObjectName("HQExportDialog");

    // 固定弹窗尺寸
    setDialogSize(scaledPx(380, 380), scaledPx(232, 232));

    // 底部"确定"按钮文字改为"导出"
    setOkButtonText(QStringLiteral("导出"));

    setupOptions();
}

// ============================================================================
// 三个导出选项单选按钮
// ============================================================================

void HQExportDialog::setupOptions()
{
    const int rowFs   = scaledPx(13, 9);
    const int spacing = scaledPx(14, 8);

    auto *optionsWidget = new QWidget(this);
    auto *optionsLayout = new QVBoxLayout(optionsWidget);
    optionsLayout->setContentsMargins(scaledPx(20, 10), scaledPx(4, 2),
                                      scaledPx(20, 10), scaledPx(4, 2));
    optionsLayout->setSpacing(spacing);

    QFont radioFont = FontManager::instance().font(rowFs, QFont::Normal);

    // 互斥分组：三个导出方式单选其一
    m_exportGroup = new QButtonGroup(this);
    m_exportGroup->setExclusive(true);

    const QString radioStyle =
        QStringLiteral("QRadioButton { color: white; padding: 0; margin: 0; spacing: 2px; }");

    // 导出选中数据单选按钮
    m_exportSelectedRb = new QRadioButton(QStringLiteral("导出选中数据"), optionsWidget);
    m_exportSelectedRb->setFont(radioFont);
    m_exportSelectedRb->setStyleSheet(radioStyle);
    m_exportGroup->addButton(m_exportSelectedRb, 0);

    // 导出当前页数据单选按钮
    m_exportCurrentRb = new QRadioButton(QStringLiteral("导出当前页数据"), optionsWidget);
    m_exportCurrentRb->setFont(radioFont);
    m_exportCurrentRb->setStyleSheet(radioStyle);
    m_exportGroup->addButton(m_exportCurrentRb, 1);

    // 导出全部数据单选按钮
    m_exportAllRb = new QRadioButton(QStringLiteral("导出全部数据"), optionsWidget);
    m_exportAllRb->setFont(radioFont);
    m_exportAllRb->setStyleSheet(radioStyle);
    m_exportGroup->addButton(m_exportAllRb, 2);

    // 默认选中"导出全部数据"
    m_exportAllRb->setChecked(true);

    // 上下加弹性空白，让三个单选按钮在内容区垂直居中
    optionsLayout->addStretch();
    optionsLayout->addWidget(m_exportSelectedRb);
    optionsLayout->addWidget(m_exportCurrentRb);
    optionsLayout->addWidget(m_exportAllRb);
    optionsLayout->addStretch();

    // 添加到内容区布局（位于标题栏与底部按钮之间）
    contentLayout()->addWidget(optionsWidget, 1);
}

// ============================================================================
// 确定按钮点击 — 按当前选中的导出方式发射对应信号
// ============================================================================

void HQExportDialog::onOkClicked()
{
    const int checkedId = m_exportGroup->checkedId();
    if (checkedId == 0) {
        emit exportSelectedClicked();
    } else if (checkedId == 1) {
        emit exportCurrentPageClicked();
    } else if (checkedId == 2) {
        emit exportAllClicked();
    } else {
        ToastWidget::instance()->showInfo(u8"请选择一种导出方式");
        return;
    }
    accept();
}
