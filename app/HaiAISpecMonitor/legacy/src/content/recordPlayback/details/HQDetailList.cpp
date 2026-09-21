#include "HQDetailList.h"
#include "HQExportDialog.h"
#include "widgets/HQCComboBox.h"
#include "widgets/HQLineEdit.h"
#include "widgets/HQToolButton.h"
#include "comm/CommonMacros.h"
#include "comm/ScreenScale.h"
#include "comm/ThemeManager.h"
#include "comm/FontManager.h"
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTableWidget>
#include <QHeaderView>
#include <QScrollBar>
#include <QPushButton>
#include <QMouseEvent>
#include <QRadialGradient>
#include <QTimer>
#include <QVector>
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <algorithm>
#include "widgets/HQCheckBox.h"
#include "radioai/icd/HQSigMF.hpp"
#include <QDateTime>
#include <QFileDialog>
#include <QRegularExpression>
#include <QSet>
#include "widgets/HQMessageBox.h"
#include "widgets/ToastWidget.h"

// ============================================================================
// HQDetailCheckBoxDelegate — 表格列复选框代理实现
// ============================================================================

void HQDetailCheckBoxDelegate::paint(QPainter *painter,
                               const QStyleOptionViewItem &option,
                               const QModelIndex &index) const
{
    QStyledItemDelegate::paint(painter, option, index);

    const auto &ss       = ScreenScale::instance();
    const bool  checked  = index.data(CheckStateRole).toInt() == Qt::Checked;
    const int   cellW    = option.rect.width();
    const int   cellH    = option.rect.height();
    const int   side     = qMin(qMin(cellW, cellH) - 4,
                                DPR_INT(16 * ss.scale(), ss.dpr(), 0));
    if (side <= 2) return;

    painter->setRenderHint(QPainter::Antialiasing, true);

    const qreal x = option.rect.center().x() - side / 2;
    const qreal y = option.rect.center().y() - side / 2;
    HQCheckBox::draw(painter, QRectF(x, y, side, side), checked);
}

bool HQDetailCheckBoxDelegate::editorEvent(QEvent *event, QAbstractItemModel *model,
                                     const QStyleOptionViewItem &option,
                                     const QModelIndex &index)
{
    if (event->type() == QEvent::MouseButtonRelease) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton && option.rect.contains(me->pos())) {
            const int cur  = index.data(CheckStateRole).toInt();
            const int next = (cur == Qt::Checked) ? Qt::Unchecked : Qt::Checked;
            model->setData(index, next, CheckStateRole);
            return true;
        }
    }
    return QStyledItemDelegate::editorEvent(event, model, option, index);
}

// ============================================================================
// HQDetailAlertDelegate — 告警状态列自绘
// ============================================================================

void HQDetailAlertDelegate::paint(QPainter *painter,
                                   const QStyleOptionViewItem &option,
                                   const QModelIndex &index) const
{
    const int mode = index.data(AlertDetailModeRole).toInt();
    if (mode != 1 && mode != 2) {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }

    const bool isAlert = (mode == 2);
    const QColor baseColor(isAlert ? QStringLiteral("#E63E3E") : QStringLiteral("#2CA25B"));
    const QString label(isAlert ? QStringLiteral("告警") : QStringLiteral("正常"));

    // 绘制选中/交替行背景
    QStyleOptionViewItem bgOpt = option;
    initStyleOption(&bgOpt, index);
    bgOpt.text.clear();
    if (const QWidget *w = option.widget)
        w->style()->drawPrimitive(QStyle::PE_PanelItemViewItem, &bgOpt, painter, w);

    // 计算布局
    const auto &ss      = ScreenScale::instance();
    const qreal dpr     = ss.dpr();
    const double scale  = ss.scale();
    const int ballSize  = DPR_INT(12 * scale, dpr, 8);
    const int spacing   = DPR_INT(4 * scale, dpr, 2);
    QFontMetrics fm(option.font);
    const int textW = fm.horizontalAdvance(label);
    const int totalW = ballSize + spacing + textW;
    const int x = option.rect.left() + (option.rect.width() - totalW) / 2;
    const int y = option.rect.top() + (option.rect.height() - ballSize) / 2;
    const QPointF ballCenter(x + ballSize / 2.0, y + ballSize / 2.0);
    const qreal radius = ballSize / 2.0;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    // 球形渐变：右上白色高光 → 主体色 → 边缘暗色
    QRadialGradient grad(ballCenter, radius,
                         QPointF(ballCenter.x() + radius * 0.4,
                                 ballCenter.y() - radius * 0.4));
    grad.setColorAt(0.0,  Qt::white);
    grad.setColorAt(0.25, baseColor.lighter(180));
    grad.setColorAt(0.6,  baseColor);
    grad.setColorAt(1.0,  baseColor.darker(150));
    painter->setBrush(grad);
    painter->setPen(Qt::NoPen);
    painter->drawEllipse(QRectF(x, y, ballSize, ballSize));

    // 文字
    painter->setPen(baseColor);
    painter->drawText(QRect(x + ballSize + spacing, option.rect.top(),
                            textW, option.rect.height()),
                      Qt::AlignLeft | Qt::AlignVCenter, label);
    painter->restore();
}

HQDetailList::HQDetailList(const QStringList &rowData, QWidget *parent)
    : QWidget(parent)
    , m_rowData(rowData)
{
    setAttribute(Qt::WA_StyledBackground, true);
    initUI();
    updateContent();
}

void HQDetailList::setData(const QStringList &rowData)
{
    m_rowData = rowData;
    m_fileName = (rowData.size() > 1) ? rowData.at(1) : QString();
    updateContent();
}

void HQDetailList::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

    const qreal w       = width();
    const qreal h       = height();
    const qreal radius  = DPR_REAL(16.0 * scale, dpr);
    const qreal borderW = DPR_REAL(1.0 * scale, dpr);
    const qreal halfBW  = borderW * 0.5;

    QPainterPath borderPath;
    borderPath.addRoundedRect(
        QRectF(halfBW, halfBW, w - borderW, h - borderW),
        radius - halfBW, radius - halfBW);

    painter.setPen(QPen(ThemeManager::instance().color("CollMonitor.borderColor"), borderW));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(borderPath);

    // 详情面板展开时，在面板顶部画全宽分割线
    if (m_detailContainer && m_detailContainer->isVisible()) {
        const int lineY = m_detailContainer->y();
        if (lineY > 0)
            painter.fillRect(0, lineY - 1, w, 1, QColor(255, 255, 255, 30));
    }
}

void HQDetailList::initUI()
{
    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

    const int padInt = static_cast<int>(DPR_REAL(24.0 * scale, dpr));

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(padInt, padInt, padInt, padInt);
    m_layout->setSpacing(DPR_INT(12 * scale, dpr, 6));

    // ============================================================
    // 顶部工具栏
    // ============================================================
    auto *toolbar = new QWidget(this);
    auto *hLay    = new QHBoxLayout(toolbar);
    hLay->setContentsMargins(0, 0, 0, 0);
    hLay->setSpacing(DPR_INT(6 * scale, dpr, 0));

    const int ctrlH = DPR_INT(28 * scale, dpr, 22);

    // 1. 排序下拉框
    m_sortCombo = new HQCComboBox(toolbar);
    m_sortCombo->addItem(QStringLiteral("按频点排序"));
    m_sortCombo->addItem(QStringLiteral("按出现次数排序"));
    m_sortCombo->setCurrentIndex(0);
    m_sortCombo->setFixedSize(DPR_INT(140 * scale, dpr, 80), ctrlH);
    m_sortCombo->setCusFont(13);
    hLay->addWidget(m_sortCombo);
    connect(m_sortCombo, &HQCComboBox::currentIndexChanged, this, [this](int) {
        m_currentPage = 1;
        refreshPage();
    });

    // 2. 起止频段
    auto freqLabel = new QLabel(toolbar);
    freqLabel->setFont(FontManager::instance().font(DPR_INT(13 * scale, dpr, 0)));
    freqLabel->setStyleSheet(QStringLiteral("color: rgb(255,255,255);"));
    freqLabel->setText(QStringLiteral("起止频段:"));

    m_freqStart = new HQLineEdit(toolbar);
    m_freqStart->setPlaceholderText(QString());
    m_freqStart->setUnit(QStringLiteral("MHz"));
    m_freqStart->setDoubleRange(0.0, 100000.0, 6);
    m_freqStart->setFixedSize(DPR_INT(100 * scale, dpr, 50), ctrlH);
    connect(m_freqStart, &HQLineEdit::editingFinished, this, [this]() {
        m_currentPage = 1;
        refreshPage();
    });

    auto *sep = new QLabel(QStringLiteral("～"), toolbar);
    sep->setFont(FontManager::instance().font(DPR_INT(13 * scale, dpr, 0)));
    sep->setStyleSheet("color: rgba(255,255,255,0.6);");
    sep->setAlignment(Qt::AlignCenter);
    sep->setFixedWidth(DPR_INT(16 * scale, dpr, 10));

    m_freqEnd = new HQLineEdit(toolbar);
    m_freqEnd->setPlaceholderText(QString());
    m_freqEnd->setUnit(QStringLiteral("MHz"));
    m_freqEnd->setDoubleRange(0.0, 100000.0, 6);
    m_freqEnd->setFixedSize(DPR_INT(100 * scale, dpr, 50), ctrlH);
    connect(m_freqEnd, &HQLineEdit::editingFinished, this, [this]() {
        m_currentPage = 1;
        refreshPage();
    });

    hLay->addWidget(freqLabel);
    hLay->addWidget(m_freqStart);
    hLay->addWidget(sep);
    hLay->addWidget(m_freqEnd);

    hLay->addSpacing(DPR_INT(8 * scale, dpr, 4));

    // 3. 仅显示告警复选框
    m_alertOnly = new HQCheckBox(toolbar);
    m_alertOnly->setText(QStringLiteral("仅显示告警"));
    m_alertOnly->setFont(FontManager::instance().font(DPR_INT(13 * scale, dpr, 0)));
    m_alertOnly->setTextColor(QColor(255, 255, 255, 255));
    hLay->addWidget(m_alertOnly);
    connect(m_alertOnly, &HQCheckBox::stateChanged, this, [this]() {
        m_currentPage = 1;
        refreshPage();
    });

    // 4. 统计标签
    const int statFs = DPR_INT(13 * scale, dpr, 0);

    // ---- 总计 ----
    m_totalText = new QLabel(QStringLiteral("总计："), toolbar);
    m_totalText->setFont(FontManager::instance().font(statFs));
    m_totalText->setStyleSheet("color: rgba(255,255,255,0.7); background: transparent;");
    hLay->addWidget(m_totalText);

    m_totalNum = new QLabel(toolbar);
    m_totalNum->setFont(FontManager::instance().font(DPR_INT(14 * scale, dpr, 0)));
    m_totalNum->setStyleSheet("color: rgba(255,255,255,0.7); font-weight: bold; background: transparent;");
    hLay->addWidget(m_totalNum);

    hLay->addSpacing(DPR_INT(16 * scale, dpr, 8));

    // ---- 告警 ----
    m_alertText = new QLabel(QStringLiteral("告警："), toolbar);
    m_alertText->setFont(FontManager::instance().font(statFs));
    m_alertText->setStyleSheet("color: rgba(255,255,255,0.7); background: transparent;");
    hLay->addWidget(m_alertText);

    m_alertNum = new QLabel(toolbar);
    m_alertNum->setFont(FontManager::instance().font(DPR_INT(14 * scale, dpr, 0)));
    m_alertNum->setStyleSheet("color: #E85C21; font-weight: bold; background: transparent;");
    hLay->addWidget(m_alertNum);
    hLay->addStretch();

    m_layout->addWidget(toolbar);

    // ============================================================
    // 信号列表表格
    // ============================================================
    m_table = new QTableWidget(0, ColumnCount, this);
    m_table->setObjectName(QStringLiteral("HQDetailListTable"));
    m_table->setHorizontalHeaderLabels({
        QString(),                              // ColCheckbox
        QStringLiteral("ID"),
        QStringLiteral("中心频率(MHz)"),
        QStringLiteral("带宽(kHz)"),
        QStringLiteral("信号类型"),
        QStringLiteral("最近出现时间"),
        QStringLiteral("出现次数"),
        QStringLiteral("平均出现时长(s)"),
        QStringLiteral("告警状态"),
        QStringLiteral("操作")
    });

    m_table->setAlternatingRowColors(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setDefaultAlignment(Qt::AlignCenter);
    m_table->setShowGrid(true);
    m_table->setFocusPolicy(Qt::NoFocus);

    // 行高 / 表头高度
    m_table->verticalHeader()->setDefaultSectionSize(
        DPR_INT(30 * scale, dpr, 20));
    m_table->horizontalHeader()->setFixedHeight(
        DPR_INT(30 * scale, dpr, 20));

    // 复选框和 ID 列按内容自适应，其余列平均分布
    m_table->horizontalHeader()->setSectionResizeMode(ColCheckbox, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(ColID, QHeaderView::ResizeToContents);
    for (int c = ColFreq; c < ColumnCount; ++c)
        m_table->horizontalHeader()->setSectionResizeMode(c, QHeaderView::Stretch);

    m_table->installEventFilter(this);

    m_cornerRadius = DPR_INT(16 * scale, dpr, 8);
    const int r16  = m_cornerRadius;
    const int pad2 = DPR_INT(2 * scale, dpr, 1);
    const int pad4 = DPR_INT(4 * scale, dpr, 2);
    const int bdr1 = DPR_INT(1 * scale, dpr, 1);

    // ---- 从主题读取颜色 ----
    const auto &tm         = ThemeManager::instance();
    const QString bgCol    = tm.colorString("hqListBox.backgroundColor");
    const QString borderCol = tm.colorString("hqListBox.borderColor");

    m_table->setItemDelegateForColumn(ColCheckbox, new HQDetailCheckBoxDelegate(m_table));
    m_table->setItemDelegateForColumn(ColAlertStatus, new HQDetailAlertDelegate(m_table));

    const QString altRowCol   = tm.colorString("hqListBox.alternateRowColor");
    const QString gridCol     = tm.colorString("hqListBox.gridlineColor");
    const QString textCol     = tm.colorString("hqListBox.textColor");
    const QString hdrBgCol    = tm.colorString("hqListBox.headerBgColor");
    const QString hdrTextCol  = tm.colorString("hqListBox.headerTextColor");
    const QString hdrBdrCol   = tm.colorString("hqListBox.headerBorderColor");
    const QString hoverCol    = tm.colorString("hqListBox.hoverRowColor");
    const QString selBgCol    = tm.colorString("hqListBox.selectedBgColor");
    const QString selTextCol  = tm.colorString("hqListBox.selectedTextColor");

    const auto kTableQss = QStringLiteral(
        "QTableWidget {"
        "  border: %6px solid %2;"
        "  border-radius: %7px;"
        "  background-color: %1;"
        "  alternate-background-color: %3;"
        "  gridline-color: %4;"
        "  font-size: 9pt;"
        "  color: %5;"
        "}"
        "QTableWidget::viewport {"
        "  background-color: transparent;"
        "  border: none;"
        "}"
        "QTableWidget::item {"
        "  padding: %8px %9px;"
        "  color: %5;"
        "}"
        "QTableWidget::item:hover {"
        "  background-color: %10;"
        "}"
        "QTableWidget::item:selected {"
        "  background-color: %11;"
        "  color: %12;"
        "}"
        "QHeaderView {"
        "  background-color: transparent;"
        "}"
        "QHeaderView::section {"
        "  background-color: %13;"
        "  color: %14;"
        "  font-weight: bold;"
        "  font-size: 9pt;"
        "  padding: %8px %9px;"
        "  border: none;"
        "  border-bottom: %6px solid %15;"
        "  border-right: %6px solid %15;"
        "}"
        "QHeaderView::section:last {"
        "  border-right: none;"
        "}"
        "QToolTip {"
        "  background-color: %2;"
        "  color: white;"
        "  border: 1px solid %4;"
        "  padding: 4px;"
        "  font-size: 9pt;"
        "}"
    );

    m_table->setStyleSheet(kTableQss
        .arg(bgCol, borderCol, altRowCol, gridCol, textCol)
        .arg(bdr1).arg(r16).arg(pad2).arg(pad4)
        .arg(hoverCol, selBgCol, selTextCol)
        .arg(hdrBgCol, hdrTextCol, hdrBdrCol));

    // ---- 全选复选框 ----
    m_checkAll = new HQCheckBox(this);
    m_checkAll->setMinBoxSize(0);
    const int chkBoxSz = DPR_INT(16 * scale, dpr, 0);
    m_checkAll->setFixedSize(chkBoxSz, chkBoxSz);
    m_checkAll->setChecked(false);

    connect(m_checkAll, &HQCheckBox::stateChanged, this, [this](int state) {
        const bool checked = (state == Qt::Checked);
        m_table->blockSignals(true);
        for (int i = 0; i < m_table->rowCount(); ++i) {
            auto *item = m_table->item(i, ColCheckbox);
            if (item) {
                item->setData(HQDetailCheckBoxDelegate::CheckStateRole,
                              checked ? Qt::Checked : Qt::Unchecked);
                if (i < m_displayIndices.size()) {
                    int rawIdx = m_displayIndices[i];
                    if (checked)
                        m_checkedRawIndices.insert(rawIdx);
                    else
                        m_checkedRawIndices.remove(rawIdx);
                }
            }
        }
        m_table->blockSignals(false);
        setSelectedCount(m_checkedRawIndices.size());
    });

    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (!item || item->column() != ColCheckbox)
            return;
        // 更新跨页勾选状态
        int row = item->row();
        if (row >= 0 && row < m_displayIndices.size()) {
            int rawIdx = m_displayIndices[row];
            if (item->data(HQDetailCheckBoxDelegate::CheckStateRole).toInt() == Qt::Checked)
                m_checkedRawIndices.insert(rawIdx);
            else
                m_checkedRawIndices.remove(rawIdx);
        }
        bool allChecked = true;
        for (int i = 0; i < m_table->rowCount(); ++i) {
            auto *it = m_table->item(i, ColCheckbox);
            if (it) {
                if (it->data(HQDetailCheckBoxDelegate::CheckStateRole).toInt() != Qt::Checked)
                    allChecked = false;
            }
        }
        m_checkAll->blockSignals(true);
        m_checkAll->setChecked(allChecked);
        m_checkAll->blockSignals(false);
        setSelectedCount(m_checkedRawIndices.size());
    });

    connect(m_table->horizontalHeader(), &QHeaderView::sectionResized,
            this, [this](int logicalIndex, int, int) {
        if (logicalIndex == ColCheckbox)
            updateCheckAllPos();
    });
    connect(m_table->horizontalHeader(), &QHeaderView::geometriesChanged,
            this, [this]() { updateCheckAllPos(); });

    // ---- 滚动条样式 ----
    const QString scHandle       = tm.colorString("scrollbar.handleColor");
    const QString scHandleHover  = tm.colorString("scrollbar.handleHoverColor");
    const QString scHandlePressed= tm.colorString("scrollbar.handlePressedColor");

    auto applyScStyle = [dpr, scale, &scHandle, &scHandleHover, &scHandlePressed](QScrollBar *sb) {
        const int sbW  = DPR_INT(8  * scale, dpr, 4);
        const int sbR  = DPR_INT(4  * scale, dpr, 2);
        const int sbMH = DPR_INT(30 * scale, dpr, 16);

        sb->setStyleSheet(QStringLiteral(
            "QScrollBar:vertical {"
            "  width: %1px;"
            "  background: transparent;"
            "  border: none;"
            "}"
            "QScrollBar::handle:vertical {"
            "  background: %4;"
            "  border: 1px solid transparent;"
            "  border-radius: %2px;"
            "  min-height: %3px;"
            "}"
            "QScrollBar::handle:vertical:hover {"
            "  background: %5;"
            "}"
            "QScrollBar::handle:vertical:pressed {"
            "  background: %6;"
            "}"
            "QScrollBar::add-line:vertical,"
            "QScrollBar::sub-line:vertical {"
            "  height: 0;"
            "  border: none;"
            "  background: transparent;"
            "}"
            "QScrollBar::add-page:vertical,"
            "QScrollBar::sub-page:vertical {"
            "  background: transparent;"
            "}"
            "QScrollBar:horizontal {"
            "  height: %1px;"
            "  background: transparent;"
            "  border: none;"
            "}"
            "QScrollBar::handle:horizontal {"
            "  background: %4;"
            "  border: 1px solid transparent;"
            "  border-radius: %2px;"
            "  min-width: %3px;"
            "}"
            "QScrollBar::handle:horizontal:hover {"
            "  background: %5;"
            "}"
            "QScrollBar::handle:horizontal:pressed {"
            "  background: %6;"
            "}"
            "QScrollBar::add-line:horizontal,"
            "QScrollBar::sub-line:horizontal {"
            "  width: 0;"
            "  border: none;"
            "  background: transparent;"
            "}"
            "QScrollBar::add-page:horizontal,"
            "QScrollBar::sub-page:horizontal {"
            "  background: transparent;"
            "}"
        ).arg(sbW).arg(sbR).arg(sbMH)
         .arg(scHandle, scHandleHover, scHandlePressed));
        sb->ensurePolished();
    };
    applyScStyle(m_table->verticalScrollBar());
    applyScStyle(m_table->horizontalScrollBar());

    m_layout->addWidget(m_table, 1);

    // ---- 底部翻页栏 ----
    createBottomBar();
    m_layout->addWidget(m_bottomBar);

    // 表格外框圆角遮罩
    QPainterPath maskPath;
    maskPath.addRoundedRect(m_table->rect(), m_cornerRadius, m_cornerRadius);
    m_table->setMask(maskPath.toFillPolygon().toPolygon());

    // 无 Demo 数据，由 SignalDetailPerFileQueryResp 推送

    // ============================================================
    // 信号详情面板（底部展开，初始隐藏）
    // ============================================================
    m_detailContainer = new QWidget(this);
    m_detailContainer->setVisible(false);
    auto *detVlay = new QVBoxLayout(m_detailContainer);
    detVlay->setContentsMargins(0, 0, 0, 0);
    detVlay->setSpacing(DPR_INT(8 * scale, dpr, 4));

    detVlay->addSpacing(DPR_INT(12 * scale, dpr, 6));

    // ---- 标题行 ----
    auto *titleRow = new QWidget(m_detailContainer);
    hLay = new QHBoxLayout(titleRow);
    hLay->setContentsMargins(0, 0, 0, 0);

    m_detailTitle = new QLabel(QStringLiteral("信号突发详情"), titleRow);
    m_detailTitle->setFont(FontManager::instance().font(
        DPR_INT(15 * scale, dpr, 0), QFont::Bold));
    m_detailTitle->setStyleSheet("color: rgb(255,255,255);");
    hLay->addWidget(m_detailTitle);
    hLay->addStretch();

    m_closeBtn = new QPushButton(titleRow);
    m_closeBtn->setIcon(QIcon(QStringLiteral(":/recordplayback/close.png")));
    m_closeBtn->setIconSize(QSize(16, 16));
    m_closeBtn->setFixedSize(24, 24);
    m_closeBtn->setFlat(true);
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    m_closeBtn->setStyleSheet(
        "QPushButton { background: transparent; border: none; }"
        "QPushButton:hover { background: rgba(255,255,255,25); border-radius: 3px; }");
    hLay->addWidget(m_closeBtn);

    connect(m_closeBtn, &QPushButton::clicked, this, [this]() {
        m_detailContainer->setVisible(false);
        update();  // 刷新分割线
    });

    detVlay->addWidget(titleRow);

    // ---- 信息行：ID / 中心频率 / 带宽 ----
    const int infoFs = DPR_INT(13 * scale, dpr, 0);
    const int valFs  = DPR_INT(14 * scale, dpr, 0);
    const auto &fm   = FontManager::instance();

    auto *infoRow = new QWidget(m_detailContainer);
    hLay = new QHBoxLayout(infoRow);
    hLay->setContentsMargins(0, 0, 0, 0);
    hLay->setSpacing(DPR_INT(16 * scale, dpr, 8));

    struct { const char *name; QLabel **ppVal; } fields[] = {
        { u8"ID：", &m_idVal },
        { u8"中心频率：", &m_freqVal },
        { u8"带宽：", &m_bwVal },
    };
    for (auto &f : fields) {
        auto *lbl = new QLabel(QString::fromUtf8(f.name), infoRow);
        lbl->setFont(fm.font(infoFs));
        lbl->setStyleSheet("color: rgba(255,255,255,0.5);");
        hLay->addWidget(lbl);

        *f.ppVal = new QLabel(QStringLiteral("-"), infoRow);
        (*f.ppVal)->setFont(fm.font(valFs, QFont::Bold));
        (*f.ppVal)->setStyleSheet("color: rgb(255,255,255);");
        hLay->addWidget(*f.ppVal);
        hLay->addSpacing(DPR_INT(32 * scale, dpr, 8));
    }
    hLay->addStretch();

    detVlay->addWidget(infoRow);

    // ---- 时间线表格 ----
    m_detailTable = new QTableWidget(0, 5, m_detailContainer);
    m_detailTable->setObjectName(QStringLiteral("HQDetailTimeTable"));
    applyScStyle(m_detailTable->verticalScrollBar());
    applyScStyle(m_detailTable->horizontalScrollBar());
    m_detailTable->setHorizontalHeaderLabels({
        QStringLiteral("出现时间"),
        QStringLiteral("消失时间"),
        QStringLiteral("持续时长"),
        QStringLiteral("中心频率"),
        QStringLiteral("带宽"),
    });
    m_detailTable->setAlternatingRowColors(true);
    m_detailTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_detailTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_detailTable->verticalHeader()->setVisible(false);
    m_detailTable->horizontalHeader()->setDefaultAlignment(Qt::AlignCenter);
    m_detailTable->setShowGrid(true);
    m_detailTable->setFocusPolicy(Qt::NoFocus);
    m_detailTable->verticalHeader()->setDefaultSectionSize(
        DPR_INT(28 * scale, dpr, 18));
    m_detailTable->horizontalHeader()->setFixedHeight(
        DPR_INT(28 * scale, dpr, 18));
    for (int c = 0; c < 5; ++c)
        m_detailTable->horizontalHeader()->setSectionResizeMode(c, QHeaderView::Stretch);

    m_detailTable->setStyleSheet(kTableQss
        .arg(bgCol, borderCol, altRowCol, gridCol, textCol)
        .arg(bdr1).arg(4).arg(pad2).arg(pad4)
        .arg(hoverCol, selBgCol, selTextCol)
        .arg(hdrBgCol, hdrTextCol, hdrBdrCol));

    m_detailTable->setFixedHeight(DPR_INT(160 * scale, dpr, 100));
    detVlay->addWidget(m_detailTable);

    m_layout->addWidget(m_detailContainer);
}

void HQDetailList::updateContent()
{
    // 总计 / 告警数
    int total  = 0;
    int alerts = 0;
    if (m_rowData.size() > 10)
        total  = m_rowData.at(10).toInt();
    if (m_rowData.size() > 11)
        alerts = m_rowData.at(11).toInt();

    m_totalNum->setText(QString::number(total));
    m_alertNum->setText(QString::number(alerts));
}

bool HQDetailList::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_table && event->type() == QEvent::Resize) {
        QPainterPath path;
        path.addRoundedRect(m_table->rect(), m_cornerRadius, m_cornerRadius);
        m_table->setMask(path.toFillPolygon().toPolygon());
    }
    return QWidget::eventFilter(obj, event);
}

void HQDetailList::addRow(const QStringList &cells)
{
    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

    const int row = m_table->rowCount();
    m_table->insertRow(row);

    // 第 0 列：复选框
    auto *checkItem = new QTableWidgetItem();
    checkItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    checkItem->setData(HQDetailCheckBoxDelegate::CheckStateRole, Qt::Unchecked);
    checkItem->setTextAlignment(Qt::AlignCenter);
    m_table->setItem(row, ColCheckbox, checkItem);

    // 第 1 ~ 8 列：数据列
    const int dataEnd = ColOper;
    const int dataCnt = cells.size();
    int alertMode = 0;
    for (int c = ColID; c < dataEnd && (c - ColID) < dataCnt; ++c) {
        const QString &txt = cells.at(c - ColID);
        auto *item = new QTableWidgetItem(txt);
        item->setTextAlignment(Qt::AlignCenter);

        // ColAlertStatus 用自定义角色存储模式，不显示文字（由委托自绘）
        if (c == ColAlertStatus) {
            if (txt == QStringLiteral("告警")) {
                item->setData(AlertDetailModeRole, 2);
                alertMode = 2;
            } else if (txt == QStringLiteral("正常")) {
                item->setData(AlertDetailModeRole, 1);
                alertMode = 1;
            }
            item->setText(QString());
        }

        item->setToolTip(txt);
        m_table->setItem(row, c, item);
    }
    for (int c = ColID + dataCnt; c < dataEnd; ++c) {
        auto *item = new QTableWidgetItem(QString());
        item->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(row, c, item);
    }

    // 根据告警状态设置行背景色
    QColor rowBg;
    if (alertMode == 2)
        rowBg = QColor(41, 15, 23);   // 告警行
    else if (alertMode == 1)
        rowBg = QColor(10, 24, 25);   // 正常行

    // ColOper：创建 QTableWidgetItem（提供背景/选中色，由 panel 透出）
    auto *operItem = new QTableWidgetItem(QString());
    operItem->setTextAlignment(Qt::AlignCenter);
    m_table->setItem(row, ColOper, operItem);

    // ColOper：3 个图标按钮（面板背景透明，让 item 的背景和选中色透出）
    auto *panel = new QWidget();
    panel->setAttribute(Qt::WA_TranslucentBackground);
    panel->installEventFilter(this);
    auto *lay = new QHBoxLayout(panel);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(8);

    struct { const char *icon; const char *tip; } btns[] = {
        { ":/recordplayback/info.png",    u8"详情" },
        { ":/recordplayback/custom.png",   u8"自定义" },
        { ":/recordplayback/delete.png",   u8"删除" },
    };

    lay->addStretch();
    QPushButton *detailBtn = nullptr;
    QPushButton *deleteBtn = nullptr;
    int btnIdx = 0;
    for (const auto &b : btns) {
        auto *btn = new QPushButton();
        btn->setIcon(QIcon(QString::fromLatin1(b.icon)));
        btn->setIconSize(QSize(DPR_INT(16 * scale, dpr, 0), DPR_INT(16 * scale, dpr, 0)));
        btn->setFixedSize(DPR_INT(22 * scale, dpr, 0), DPR_INT(22 * scale, dpr, 0));
        btn->setToolTip(QString::fromUtf8(b.tip));
        btn->setFlat(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(
            "QPushButton { background: transparent; border: none; }"
            "QPushButton:hover { background: rgba(255,255,255,25); border-radius: 3px; }");
        btn->installEventFilter(this);
        lay->addWidget(btn);
        if (btnIdx == 0)
            detailBtn = btn;
        else if (btnIdx == 2)
            deleteBtn = btn;
        ++btnIdx;
    }

    // 连接行删除按钮
    if (deleteBtn) {
        connect(deleteBtn, &QPushButton::clicked, this, [this, row]() {
            if (m_displayIndices.isEmpty() || row >= m_displayIndices.size()) return;
            int rawIdx = m_displayIndices[row];
            if (rawIdx < 0 || rawIdx >= m_rawSignalItems.size()) return;
            auto* box = HQMessageBox::getInstance();
            box->setTitle(u8"确认删除");
            box->setMsgInfo(u8"确认删除该信号？删除后不可恢复。");
            if (box->exec() != QDialog::Accepted)
                return;
            QVector<qint64> ids = { static_cast<qint64>(m_rawSignalItems[rawIdx].id) };
            emit deleteSignalsRequested(m_fileId, ids);
        });
    }

    // 连接"详情"按钮：展开底部详情面板并填充数据
    if (detailBtn) {
        connect(detailBtn, &QPushButton::clicked, this, [this, row]() {
            QStringList data;
            for (int c = ColID; c < ColOper; ++c) {
                auto *item = m_table->item(row, c);
                data << (item ? item->text() : QString());
            }

            // 填充信息行
            m_idVal->setText(data.size() > 0 ? data.at(0) : QStringLiteral("-"));
            m_freqVal->setText(data.size() > 1 ? data.at(1) : QStringLiteral("-"));
            m_bwVal->setText(data.size() > 2 ? data.at(2) : QStringLiteral("-"));

            // 填充详情表格（从 m_detailTimeData 取真实数据）
            m_detailTable->setRowCount(0);
            int dataIdx = (row >= 0 && row < m_displayIndices.size()) ? m_displayIndices[row] : -1;
            if (dataIdx >= 0 && dataIdx < m_detailTimeData.size()) {
                const auto& timeRows = m_detailTimeData[dataIdx];
                for (int i = 0; i < timeRows.size(); ++i) {
                    const int r = m_detailTable->rowCount();
                    m_detailTable->insertRow(r);
                    const auto& vals = timeRows[i];
                    for (int c = 0; c < vals.size() && c < 5; ++c) {
                        auto *item = new QTableWidgetItem(vals[c]);
                        item->setTextAlignment(Qt::AlignCenter);
                        m_detailTable->setItem(r, c, item);
                    }
                }
            }

            m_detailContainer->setVisible(true);
            update();
        });
    }
    lay->addStretch();

    // 设置行背景色（含 ColOper 的 item，面板透明会透出 item 背景和选中色）
    if (rowBg.isValid()) {
        auto setBg = [&](int col) {
            if (auto *it = m_table->item(row, col))
                it->setBackground(rowBg);
        };
        setBg(ColCheckbox);
        for (int c = ColID; c <= ColOper; ++c)
            setBg(c);
    }

    m_table->setCellWidget(row, ColOper, panel);
}

void HQDetailList::clear()
{
    m_table->setRowCount(0);
    m_checkedRawIndices.clear();
    m_checkAll->blockSignals(true);
    m_checkAll->setChecked(false);
    m_checkAll->blockSignals(false);
    m_detailTimeData.clear();
    m_detailContainer->setVisible(false);
    setSelectedCount(0);
}

void HQDetailList::setTotalCount(int count)
{
    m_totalCount = count;
    m_pageTotalLabel->setText(QStringLiteral("共 %1 条").arg(count));
}

void HQDetailList::setCurrentPage(int page)
{
    if (page < 1)          page = 1;
    if (page > m_totalPage) page = m_totalPage;
    m_currentPage = page;
    updatePageButtons();
    refreshPage();
}

void HQDetailList::setSignalData(const QVector<QStringList>& data)
{
    m_signalData = data;
    m_totalCount = data.size();
    m_pageSize = m_pageSizeCombo->currentData().toInt();
    m_totalPage = (m_totalCount + m_pageSize - 1) / m_pageSize;
    if (m_totalPage < 1) m_totalPage = 1;
    m_currentPage = 1;
    refreshPage();
    m_totalNum->setText(QString::number(m_totalCount));
    m_pageTotalLabel->setText(QStringLiteral("共 %1 条").arg(m_totalCount));
    updatePageButtons();
}

void HQDetailList::setDetailTimeData(const QVector<QVector<QStringList>>& data)
{
    m_detailTimeData = data;
}

static QString csvEscape(const QString& s)
{
    if (s.contains(',') || s.contains('"') || s.contains('\n'))
        return '"' + QString(s).replace("\"", "\"\"") + '"';
    return s;
}

static QString sanitizeFileName(const QString& name)
{
    QString safe = name;
    safe.replace(QRegularExpression(R"([\\/:*?"<>|])"), QStringLiteral("_"));
    return safe;
}

bool HQDetailList::exportToCsv(const QString& dirPath) const
{
    if (m_rawSignalItems.isEmpty()) return false;

    QString prefix = m_fileName.isEmpty() ? QString() : sanitizeFileName(m_fileName) + "_";

    QSet<int> checkedRawIdx = m_checkedRawIndices;
    // 未勾选时默认导出全部信号（保障文件列表导出能附带信号明细）
    if (checkedRawIdx.isEmpty()) {
        return false;
    }

    // {filename}_spectrum_file_signals.csv — 仅勾选信号
    {
        QFile file(dirPath + "/" + prefix + "spectrum_file_signals.csv");
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
        QTextStream out(&file);
        out.setCodec("UTF-8");
        out << "file_id,signal_id,fc(MHz),bw(kHz),carry_type,recent_time,burst_num,avg_duration(s)\n";
        for (int idx : checkedRawIdx) {
            if (idx < 0 || idx >= m_rawSignalItems.size()) continue;
            const auto& sig = m_rawSignalItems[idx];
            out << m_fileId << ","
                << sig.id << ","
                << QString::number(sig.fc / 1e6, 'f', 6) << ","
                << QString::number(sig.bw / 1e3, 'f', 3) << ","
                << sig.carry_type << ","
                << QDateTime::fromMSecsSinceEpoch(sig.recent_time / NS_PER_MS).toString("yyyy-MM-dd hh:mm:ss.zzz") << ","
                << sig.burst_num << ","
                << QString::number((sig.avg_duration / NS_PER_MS) / 1000.0, 'f', 3) << "\n";
        }
    }

    // {filename}_signal_time_ranges.csv — 仅勾选信号对应的时间线
    {
        QFile file(dirPath + "/" + prefix + "signal_time_ranges.csv");
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
        QTextStream out(&file);
        out.setCodec("UTF-8");
        out << "file_id,signal_id,bgn_time,end_time,duration(s)\n";
        for (int idx : checkedRawIdx) {
            if (idx < 0 || idx >= m_rawSignalItems.size()) continue;
            const auto& sig = m_rawSignalItems[idx];
            for (const auto& t : sig.times) {
                out << m_fileId << "," << sig.id << ","
                    << QDateTime::fromMSecsSinceEpoch(t.first / NS_PER_MS).toString("yyyy-MM-dd hh:mm:ss.zzz") << ","
                    << QDateTime::fromMSecsSinceEpoch(t.second / NS_PER_MS).toString("yyyy-MM-dd hh:mm:ss.zzz") << ","
                    << QString::number((t.second - t.first) / NS_PER_MS / 1000.0, 'f', 3) << "\n";
            }
        }
    }
    ToastWidget::instance()->showInfo(u8"导出选中数据成功");
    return true;
}

bool HQDetailList::exportCurPageToCsv(const QString& dirPath) const
{
    // 当前页没有信号数据时直接返回失败
    if (m_rawSignalItems.isEmpty() || m_displayIndices.isEmpty()) return false;

    QString prefix = m_fileName.isEmpty() ? QString() : sanitizeFileName(m_fileName) + "_";

    // {filename}_spectrum_file_signals.csv — 当前页信号
    {
        QFile file(dirPath + "/" + prefix + "spectrum_file_signals.csv");
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
        QTextStream out(&file);
        out.setCodec("UTF-8");
        out << "file_id,signal_id,fc(MHz),bw(kHz),carry_type,recent_time,burst_num,avg_duration(s)\n";
        for (int idx : m_displayIndices) {
            if (idx < 0 || idx >= m_rawSignalItems.size()) continue;
            const auto& sig = m_rawSignalItems[idx];
            out << m_fileId << ","
                << sig.id << ","
                << QString::number(sig.fc / 1e6, 'f', 6) << ","
                << QString::number(sig.bw / 1e3, 'f', 3) << ","
                << sig.carry_type << ","
                << QDateTime::fromMSecsSinceEpoch(sig.recent_time / NS_PER_MS).toString("yyyy-MM-dd hh:mm:ss.zzz") << ","
                << sig.burst_num << ","
                << QString::number((sig.avg_duration / NS_PER_MS) / 1000.0, 'f', 3) << "\n";
        }
    }

    // {filename}_signal_time_ranges.csv — 当前页信号对应的时间线
    {
        QFile file(dirPath + "/" + prefix + "signal_time_ranges.csv");
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
        QTextStream out(&file);
        out.setCodec("UTF-8");
        out << "file_id,signal_id,bgn_time,end_time,duration(s)\n";
        for (int idx : m_displayIndices) {
            if (idx < 0 || idx >= m_rawSignalItems.size()) continue;
            const auto& sig = m_rawSignalItems[idx];
            for (const auto& t : sig.times) {
                out << m_fileId << "," << sig.id << ","
                    << QDateTime::fromMSecsSinceEpoch(t.first / NS_PER_MS).toString("yyyy-MM-dd hh:mm:ss.zzz") << ","
                    << QDateTime::fromMSecsSinceEpoch(t.second / NS_PER_MS).toString("yyyy-MM-dd hh:mm:ss.zzz") << ","
                    << QString::number((t.second - t.first) / NS_PER_MS / 1000.0, 'f', 3) << "\n";
            }
        }
    }
    ToastWidget::instance()->showInfo(u8"导出当前页数据成功");
    return true;
}

bool HQDetailList::exportAllToCsv(const QString& dirPath) const
{
    // 没有已加载的信号数据时直接返回失败
    if (m_rawSignalItems.isEmpty()) return false;

    QString prefix = m_fileName.isEmpty() ? QString() : sanitizeFileName(m_fileName) + "_";

    // {filename}_spectrum_file_signals.csv — 全部信号
    {
        QFile file(dirPath + "/" + prefix + "spectrum_file_signals.csv");
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
        QTextStream out(&file);
        out.setCodec("UTF-8");
        out << "file_id,signal_id,fc(MHz),bw(kHz),carry_type,recent_time,burst_num,avg_duration(s)\n";
        for (int idx = 0; idx < m_rawSignalItems.size(); ++idx) {
            const auto& sig = m_rawSignalItems[idx];
            out << m_fileId << ","
                << sig.id << ","
                << QString::number(sig.fc / 1e6, 'f', 6) << ","
                << QString::number(sig.bw / 1e3, 'f', 3) << ","
                << sig.carry_type << ","
                << QDateTime::fromMSecsSinceEpoch(sig.recent_time / NS_PER_MS).toString("yyyy-MM-dd hh:mm:ss.zzz") << ","
                << sig.burst_num << ","
                << QString::number((sig.avg_duration / NS_PER_MS) / 1000.0, 'f', 3) << "\n";
        }
    }

    // {filename}_signal_time_ranges.csv — 全部信号对应的时间线
    {
        QFile file(dirPath + "/" + prefix + "signal_time_ranges.csv");
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
        QTextStream out(&file);
        out.setCodec("UTF-8");
        out << "file_id,signal_id,bgn_time,end_time,duration(s)\n";
        for (int idx = 0; idx < m_rawSignalItems.size(); ++idx) {
            const auto& sig = m_rawSignalItems[idx];
            for (const auto& t : sig.times) {
                out << m_fileId << "," << sig.id << ","
                    << QDateTime::fromMSecsSinceEpoch(t.first / NS_PER_MS).toString("yyyy-MM-dd hh:mm:ss.zzz") << ","
                    << QDateTime::fromMSecsSinceEpoch(t.second / NS_PER_MS).toString("yyyy-MM-dd hh:mm:ss.zzz") << ","
                    << QString::number((t.second - t.first) / NS_PER_MS / 1000.0, 'f', 3) << "\n";
            }
        }
    }
    ToastWidget::instance()->showInfo(u8"导出全部数据成功");
    return true;
}

void HQDetailList::refreshPage()
{
    // Save current page checkbox states before clearing
    for (int i = 0; i < m_table->rowCount() && i < m_displayIndices.size(); ++i) {
        auto *item = m_table->item(i, ColCheckbox);
        if (item) {
            int rawIdx = m_displayIndices[i];
            if (item->data(HQDetailCheckBoxDelegate::CheckStateRole).toInt() == Qt::Checked)
                m_checkedRawIndices.insert(rawIdx);
            else
                m_checkedRawIndices.remove(rawIdx);
        }
    }

    m_table->setRowCount(0);
    if (m_signalData.isEmpty()) return;

    // Build filtered index list
    QVector<int> indices;
    indices.reserve(m_signalData.size());
    const int freqCol = ColFreq - ColID;
    const int alertCol = ColAlertStatus - ColID;
    const bool alertFilter = m_alertOnly->isChecked();
    const QString freqStartText = m_freqStart->text().trimmed();
    const QString freqEndText = m_freqEnd->text().trimmed();
    const bool hasFreqStart = !freqStartText.isEmpty();
    const bool hasFreqEnd = !freqEndText.isEmpty();
    double freqStartVal = freqStartText.toDouble();
    double freqEndVal = freqEndText.toDouble();

    for (int i = 0; i < m_signalData.size(); ++i) {
        const QStringList &row = m_signalData[i];
        bool pass = true;

        if (pass && alertFilter) {
            if (alertCol >= row.size() || row[alertCol] != QStringLiteral("告警"))
                pass = false;
        }

        if (pass && (hasFreqStart || hasFreqEnd)) {
            if (freqCol < row.size()) {
                bool ok = false;
                double freq = row[freqCol].toDouble(&ok);
                if (ok) {
                    if (hasFreqStart && freq < freqStartVal)
                        pass = false;
                    if (pass && hasFreqEnd && freq > freqEndVal)
                        pass = false;
                }
            }
        }

        if (pass)
            indices.append(i);
    }

    // Sort indices
    const int sortIdx = m_sortCombo->currentIndex();
    if (sortIdx == 1) {
        // 按出现次数排序 (descending)
        const int cntCol = ColTotalCount - ColID;
        std::sort(indices.begin(), indices.end(), [&](int a, int b) {
            int va = (cntCol < m_signalData[a].size()) ? m_signalData[a][cntCol].toInt() : 0;
            int vb = (cntCol < m_signalData[b].size()) ? m_signalData[b][cntCol].toInt() : 0;
            return va > vb;
        });
    } else {
        // 按频点排序 (ascending, default)
        std::sort(indices.begin(), indices.end(), [&](int a, int b) {
            double va = (freqCol < m_signalData[a].size()) ? m_signalData[a][freqCol].toDouble() : 0;
            double vb = (freqCol < m_signalData[b].size()) ? m_signalData[b][freqCol].toDouble() : 0;
            return va < vb;
        });
    }

    // Recalculate pagination for filtered view
    m_totalCount = indices.size();
    m_totalPage = qMax(1, (m_totalCount + m_pageSize - 1) / m_pageSize);
    if (m_currentPage > m_totalPage)
        m_currentPage = m_totalPage;

    int start = (m_currentPage - 1) * m_pageSize;
    int end = qMin(start + m_pageSize, indices.size());
    m_displayIndices.clear();
    m_displayIndices.reserve(end - start);
    for (int r = start; r < end; ++r) {
        addRow(m_signalData[indices[r]]);
        m_displayIndices.append(indices[r]);
    }

    // Restore checkbox states from m_checkedRawIndices
    for (int i = 0; i < m_displayIndices.size(); ++i) {
        int rawIdx = m_displayIndices[i];
        if (m_checkedRawIndices.contains(rawIdx)) {
            auto *item = m_table->item(i, ColCheckbox);
            if (item)
                item->setData(HQDetailCheckBoxDelegate::CheckStateRole, Qt::Checked);
        }
    }

    m_totalNum->setText(QString::number(m_totalCount));
    m_pageTotalLabel->setText(QStringLiteral("共 %1 条").arg(m_totalCount));
    updatePageButtons();
    setSelectedCount(m_checkedRawIndices.size());
}

void HQDetailList::setSelectedCount(int count)
{
    m_selectedLabel->setText(QStringLiteral("已选择 <span style='color:#E85C21;'>%1</span> 条").arg(count));
    bool hasChecked = (count > 0);
    if (m_deleteBtn) m_deleteBtn->setEnabled(hasChecked);
}

void HQDetailList::setTotalPage(int pages)
{
    m_totalPage = (pages < 1) ? 1 : pages;
    if (m_currentPage > m_totalPage)
        m_currentPage = m_totalPage;
    updatePageButtons();
}

// ============================================================================
// 底部翻页栏
// ============================================================================

void HQDetailList::createBottomBar()
{
    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();
    const auto &tm     = ThemeManager::instance();

    const int barH = DPR_INT(32 * scale, dpr, 24);

    m_bottomBar = new QWidget(this);
    m_bottomBar->setFixedHeight(barH);

    auto *lay = new QHBoxLayout(m_bottomBar);
    lay->setContentsMargins(DPR_INT(8 * scale, dpr, 4), 0,
                            DPR_INT(8 * scale, dpr, 4), 0);
    lay->setSpacing(DPR_INT(8 * scale, dpr, 4));

    // ---- 左侧：统计 ----
    m_pageTotalLabel = new QLabel(m_bottomBar);
    m_pageTotalLabel->setTextFormat(Qt::RichText);
    m_pageTotalLabel->setText(QStringLiteral("共 0 条"));
    m_pageTotalLabel->setFixedHeight(barH);
    m_pageTotalLabel->setAlignment(Qt::AlignVCenter);
    lay->addWidget(m_pageTotalLabel);

    m_selectedLabel = new QLabel(m_bottomBar);
    m_selectedLabel->setTextFormat(Qt::RichText);
    m_selectedLabel->setText(QStringLiteral("已选择 <span style='color:#E85C21;'>0</span> 条"));
    m_selectedLabel->setFixedHeight(barH);
    m_selectedLabel->setAlignment(Qt::AlignVCenter);
    lay->addWidget(m_selectedLabel);

    // 导出 / 删除按钮
    const int iconBtnH = DPR_INT(28 * scale, dpr, 22);
    const int iconBtnW = DPR_INT(79 * scale, dpr, 79);
    {
        auto *expBtn = new HQToolButton(m_bottomBar);
        expBtn->setHQIcon(QIcon(QStringLiteral(":/recordplayback/export.png")), 12, 12);
        expBtn->setText(QStringLiteral("导出 "));
        expBtn->setFixedSize(iconBtnW, iconBtnH);
        expBtn->setHQRadius(6);
        expBtn->setTextColor(QColor(Qt::white));
        m_exportBtn = expBtn;
        // 导出按钮：点击后弹出导出数据弹窗，由弹窗内的选项触发实际导出
        connect(m_exportBtn, &QPushButton::clicked, this, [this]() {
            HQExportDialog dlg(this);
            // 导出选中数据：复用既有 exportClicked 信号，走原导出流程
            connect(&dlg, &HQExportDialog::exportSelectedClicked,
                    this, [this]()
                    {
                        QString dir = QFileDialog::getExistingDirectory(this, u8"选择导出目录");
                        if (dir.isEmpty()) return;
                        bool res = exportToCsv(dir);
                        if (!res) ToastWidget::instance()->showWarning(u8"未选中导出数据");
                    });
            // 导出当前页数据：功能暂未实现，先预留扩展点
            connect(&dlg, &HQExportDialog::exportCurrentPageClicked,
                    this, [this]()
                    {
                        QString dir = QFileDialog::getExistingDirectory(this, u8"选择导出目录");
                        if (dir.isEmpty()) return;
                        bool res = exportCurPageToCsv(dir);
                        if (!res) ToastWidget::instance()->showWarning(u8"无数据导出");
                    });
            // 导出全部数据：功能暂未实现，先预留扩展点
            connect(&dlg, &HQExportDialog::exportAllClicked,
                    this, [this]()
                    {
                        QString dir = QFileDialog::getExistingDirectory(this, u8"选择导出目录");
                        if (dir.isEmpty()) return;
                        bool res = exportAllToCsv(dir);
                        if (!res) ToastWidget::instance()->showWarning(u8"无数据导出");
                    });
            dlg.exec();
        });
        lay->addWidget(m_exportBtn);

        auto *delBtn = new HQToolButton(m_bottomBar);
        delBtn->setHQIcon(QIcon(QStringLiteral(":/recordplayback/delete.png")), 12, 12);
        delBtn->setText(QStringLiteral("删除 "));
        delBtn->setFixedSize(iconBtnW, iconBtnH);
        delBtn->setHQRadius(6);
        delBtn->setTextColor(QColor(230, 62, 62));
        m_deleteBtn = delBtn;
        m_deleteBtn->setEnabled(false);
        connect(m_deleteBtn, &QPushButton::clicked, this, [this]() {
            QVector<int> checkedRawIndices;
            checkedRawIndices.reserve(m_checkedRawIndices.size());
            for (int idx : m_checkedRawIndices)
                checkedRawIndices.append(idx);
            if (checkedRawIndices.isEmpty()) {
                ToastWidget::instance()->showInfo(u8"请先勾选要删除的信号");
                return;
            }
            auto* box = HQMessageBox::getInstance();
            box->setTitle(u8"确认删除");
            box->setMsgInfo(QStringLiteral("确认删除选中的 %1 个信号？删除后不可恢复。").arg(checkedRawIndices.size()));
            if (box->exec() != QDialog::Accepted)
                return;
            QVector<qint64> ids;
            ids.reserve(checkedRawIndices.size());
            for (int idx : checkedRawIndices) {
                if (idx >= 0 && idx < m_rawSignalItems.size())
                    ids.append(static_cast<qint64>(m_rawSignalItems[idx].id));
            }
            if (!ids.isEmpty())
                emit deleteSignalsRequested(m_fileId, ids);
        });
        lay->addWidget(m_deleteBtn);
    }

    lay->addStretch();

    // ---- 右侧：翻页 ----
    const int btnSize = DPR_INT(28 * scale, dpr, 20);

    m_prevBtn = new QPushButton(QStringLiteral("<"), m_bottomBar);
    m_prevBtn->setFixedSize(btnSize, btnSize);
    m_prevBtn->setFlat(true);
    m_prevBtn->setCursor(Qt::PointingHandCursor);
    m_prevBtn->setEnabled(false);

    m_pageBox = new QWidget(m_bottomBar);
    auto *pageLay = new QHBoxLayout(m_pageBox);
    pageLay->setContentsMargins(0, 0, 0, 0);
    pageLay->setSpacing(2);

    m_nextBtn = new QPushButton(QStringLiteral(">"), m_bottomBar);
    m_nextBtn->setFixedSize(btnSize, btnSize);
    m_nextBtn->setFlat(true);
    m_nextBtn->setCursor(Qt::PointingHandCursor);
    m_nextBtn->setEnabled(false);

    lay->addWidget(m_prevBtn);
    lay->addWidget(m_pageBox);
    lay->addWidget(m_nextBtn);

    // ---- 每页条数下拉 ----
    m_pageSizeCombo = new HQCComboBox(m_bottomBar);
    m_pageSizeCombo->addItem(QStringLiteral("50条/页"), 50);
    m_pageSizeCombo->addItem(QStringLiteral("100条/页"), 100);
    m_pageSizeCombo->addItem(QStringLiteral("200条/页"), 200);
    m_pageSizeCombo->setCurrentIndex(0);
    m_pageSizeCombo->setFixedSize(DPR_INT(92 * scale, dpr, 92),
                                  DPR_INT(28 * scale, dpr, 22));
    m_pageSizeCombo->setCusFont(12);
    lay->addWidget(m_pageSizeCombo);
    connect(m_pageSizeCombo, &HQCComboBox::currentIndexChanged, this, [this](int) {
        emit pageSizeChanged(m_pageSizeCombo->currentData().toInt());
    });

    // ---- 跳转页 ----
    auto *jumpLabel = new QLabel(QStringLiteral("跳至"), m_bottomBar);
    jumpLabel->setFixedHeight(barH);
    jumpLabel->setAlignment(Qt::AlignVCenter);
    jumpLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 9pt; background: transparent; }")
        .arg(tm.colorString("hqListBox.textColor")));
    lay->addWidget(jumpLabel);

    m_jumpInput = new HQLineEdit(m_bottomBar);
    m_jumpInput->setPlaceholderText(QString());
    m_jumpInput->setFixedSize(DPR_INT(60 * scale, dpr, 36),
                               DPR_INT(28 * scale, dpr, 22));
    m_jumpInput->setDoubleRange(1, 9999999, 0);
    lay->addWidget(m_jumpInput);

    auto *pageLabel = new QLabel(QStringLiteral("页"), m_bottomBar);
    pageLabel->setFixedHeight(barH);
    pageLabel->setAlignment(Qt::AlignVCenter);
    pageLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 9pt; background: transparent; }")
        .arg(tm.colorString("hqListBox.textColor")));
    lay->addWidget(pageLabel);
    connect(m_jumpInput, &HQLineEdit::editingFinished, this, [this]() {
        bool ok = false;
        int page = m_jumpInput->text().toInt(&ok);
        if (ok && page >= 1 && page <= m_totalPage) {
            setCurrentPage(page);
            emit pageChanged(m_currentPage);
        }
        m_jumpInput->setText(QString());
    });

    // ---- 连接信号 ----
    auto updatePage = [this]() {
        updatePageButtons();
        refreshPage();
    };
    connect(m_prevBtn, &QPushButton::clicked, this, [this, updatePage]() {
        if (m_currentPage > 1) {
            --m_currentPage;
            updatePage();
            emit pageChanged(m_currentPage);
        }
    });
    connect(m_nextBtn, &QPushButton::clicked, this, [this, updatePage]() {
        if (m_currentPage < m_totalPage) {
            ++m_currentPage;
            updatePage();
            emit pageChanged(m_currentPage);
        }
    });
    // 每页条数变更时重新计算总页数
    connect(m_pageSizeCombo, &HQCComboBox::currentIndexChanged, this, [this, updatePage](int) {
        m_pageSize = m_pageSizeCombo->currentData().toInt();
        m_totalPage = (m_totalCount + m_pageSize - 1) / m_pageSize;
        if (m_totalPage < 1) m_totalPage = 1;
        if (m_currentPage > m_totalPage) m_currentPage = m_totalPage;
        updatePage();
    });

    // ---- 样式 ----
    const QString textCol = tm.colorString("hqListBox.textColor");
    const QString bdrCol  = tm.colorString("hqListBox.borderColor");

    m_pageTotalLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 9pt; background: transparent; }").arg(textCol));
    m_selectedLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 9pt; background: transparent; }").arg(textCol));

    const QString navBtnSs = QStringLiteral(
        "QPushButton { background: transparent; border: 1px solid %1;"
        "  border-radius: 4px; color: %2; font-size: 9pt; }"
        "QPushButton:hover { background: rgba(255,255,255,25); }"
        "QPushButton:disabled { color: #555; border-color: #444; }"
    ).arg(bdrCol, textCol);
    m_prevBtn->setStyleSheet(navBtnSs);
    m_nextBtn->setStyleSheet(navBtnSs);

    m_bottomBar->setStyleSheet(QStringLiteral(
        "QWidget { background-color: %1; }"
    ).arg(tm.colorString("hqListBox.backgroundColor")));

    updatePageButtons();
}

void HQDetailList::updatePageButtons()
{
    // 清除旧页码按钮
    QLayoutItem *child;
    while ((child = m_pageBox->layout()->takeAt(0)) != nullptr) {
        if (child->widget())
            child->widget()->deleteLater();
        delete child;
    }

    m_prevBtn->setEnabled(m_currentPage > 1);
    m_nextBtn->setEnabled(m_currentPage < m_totalPage);

    if (m_totalPage <= 1)
        return;

    // 构建要显示的页码集合（首尾 + 当前页附近）
    QVector<int> pages;
    pages.reserve(9);
    const int first = 1;
    const int last  = m_totalPage;

    for (int i = m_currentPage - 2; i <= m_currentPage + 2; ++i) {
        if (i >= first && i <= last && !pages.contains(i))
            pages.append(i);
    }
    if (!pages.contains(first)) pages.prepend(first);
    if (!pages.contains(last))  pages.append(last);
    std::sort(pages.begin(), pages.end());

    // 添加页码按钮，中间跳过时插入省略号
    const auto &ss      = ScreenScale::instance();
    const auto &tm      = ThemeManager::instance();
    const int   fsPx    = DPR_INT(12 * ss.scale(), ss.dpr(), 12);
    const QString textCol = tm.colorString("hqListBox.textColor");
    const QString bdrCol  = tm.colorString("hqListBox.borderColor");
    const QString accCol  = QStringLiteral("#0A8CFE");

    const QString activeBtnSs = QStringLiteral(
        "QPushButton { background: transparent; border: 1px solid %1;"
        "  border-radius: 3px; color: %2; font-size: %3px; }"
        "QPushButton:hover { background: rgba(255,255,255,25); }"
    ).arg(bdrCol, textCol).arg(fsPx);

    const QString curBtnSs = QStringLiteral(
        "QPushButton { background: rgba(10,140,254,0.25); border: 1px solid %1;"
        "  border-radius: 3px; color: %1; font-size: %2px; font-weight: bold; }"
    ).arg(accCol).arg(fsPx);

    int prevPage = 0;
    for (int p : pages) {
        if (prevPage > 0 && p - prevPage > 1) {
            auto *dot = new QLabel(QStringLiteral("..."), m_pageBox);
            dot->setFixedWidth(DPR_INT(20 * ScreenScale::instance().scale(),
                                       ScreenScale::instance().dpr(), 16));
            dot->setAlignment(Qt::AlignCenter);
            dot->setStyleSheet(QStringLiteral(
                "color: #666; font-size: %1px; background: transparent;").arg(fsPx));
            m_pageBox->layout()->addWidget(dot);
        }

        auto *btn = new QPushButton(QString::number(p), m_pageBox);
        btn->setFixedSize(DPR_INT(28 * ScreenScale::instance().scale(),
                                  ScreenScale::instance().dpr(), 20),
                          DPR_INT(28 * ScreenScale::instance().scale(),
                                  ScreenScale::instance().dpr(), 20));
        btn->setFlat(true);
        btn->setCursor(Qt::PointingHandCursor);

        if (p == m_currentPage) {
            btn->setEnabled(false);
            btn->setStyleSheet(curBtnSs);
        } else {
            btn->setStyleSheet(activeBtnSs);
            connect(btn, &QPushButton::clicked, this, [this, p]() {
                m_currentPage = p;
                updatePageButtons();
                refreshPage();
                emit pageChanged(m_currentPage);
            });
        }
        m_pageBox->layout()->addWidget(btn);
        prevPage = p;
    }
}

void HQDetailList::updateCheckAllPos()
{
    if (!m_checkAll || !m_table->horizontalHeader())
        return;

    // 将表格表头区域坐标映射到父控件坐标系
    const int  secX  = m_table->horizontalHeader()->sectionViewportPosition(ColCheckbox);
    const QPoint tblOrg = m_table->mapTo(this, QPoint(0, 0));
    const int  w     = m_table->horizontalHeader()->sectionSize(ColCheckbox);
    const int  h     = m_table->horizontalHeader()->height();
    const int  cw    = m_checkAll->width();
    const int  ch    = m_checkAll->height();

    const int posX = tblOrg.x() + secX + qMax(0, (w - cw) / 2);
    const int posY = tblOrg.y() + (h > 0 ? qMax(0, (h - ch) / 2) : 0);
    m_checkAll->move(posX, posY);
}
