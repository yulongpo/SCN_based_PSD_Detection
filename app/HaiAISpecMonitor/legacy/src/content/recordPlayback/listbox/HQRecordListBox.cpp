#include "HQRecordListBox.h"
#include "comm/ScreenScale.h"
#include "comm/CommonMacros.h"
#include "comm/ThemeManager.h"
#include <QTableWidget>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QScrollBar>
#include <QStyleFactory>
#include <QPainterPath>
#include <QStyle>
#include <QFontMetrics>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QTimer>
#include <QMouseEvent>
#include <QAbstractItemModel>
#include "widgets/HQCComboBox.h"
#include "widgets/HQLineEdit.h"
#include "widgets/HQToolButton.h"
#include <QVector>
#include <algorithm>

#include "comm/FontManager.h"
#include "../RecordDialog.h"
#include "spdlog/common.h"
#include "spdlog/spdlog.h"

// ============================================================================
// HQCheckBoxDelegate — 表格列复选框代理实现
// ============================================================================

void HQCheckBoxDelegate::paint(QPainter *painter,
                               const QStyleOptionViewItem &option,
                               const QModelIndex &index) const
{
    QStyledItemDelegate::paint(painter, option, index);

    const auto &ss   = ScreenScale::instance();
    const bool checked = index.data(CheckStateRole).toInt() == Qt::Checked;
    const int cellW = option.rect.width();
    const int cellH = option.rect.height();
    const int side  = qMin(qMin(cellW, cellH) - 4,
                           DPR_INT(16 * ss.scale(), ss.dpr(), 0));
    if (side <= 2) return;

    painter->setRenderHint(QPainter::Antialiasing, true);

    const qreal x = option.rect.center().x() - side / 2;
    const qreal y = option.rect.center().y() - side / 2;
    HQCheckBox::draw(painter, QRectF(x, y, side, side), checked);
}

bool HQCheckBoxDelegate::editorEvent(QEvent *event, QAbstractItemModel *model,
                                     const QStyleOptionViewItem &option,
                                     const QModelIndex &index)
{
    if (event->type() == QEvent::MouseButtonRelease) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton && option.rect.contains(me->pos())) {
            const int cur = index.data(CheckStateRole).toInt();
            const int next = (cur == Qt::Checked) ? Qt::Unchecked : Qt::Checked;
            model->setData(index, next, CheckStateRole);
            return true;
        }
    }
    return QStyledItemDelegate::editorEvent(event, model, option, index);
}

// ============================================================================
// 列宽度基准值（构造时按 DPR 缩放）
// ============================================================================
static const int kBaseColW[HQRecordListBox::ColumnCount] = {
    32,   // 复选框
    40,   // 序号
    250,  // 文件名
    70,   // 采集设备
    140,  // 起始频率(MHz)
    140,  // 结束频率(MHz)
    70,   // RBW(kHz)
    200,  // 开始时间
    200,  // 结束时间
    80,   // 总时长
    80,   // 大小
    80,   // 信号数
    80,   // 告警数
    150,  // 操作
};

HQRecordListBox::HQRecordListBox(QWidget *parent)
    : QWidget(parent)
{
    // ---- DPR 缩放参数 ----
    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

    m_realFileInfo.file_id = -1;

    // ---- 表格 ----
    m_table = new QTableWidget(0, ColumnCount, this);
    m_table->setObjectName(QStringLiteral("HQRecordListBox"));
    m_table->setHorizontalHeaderLabels({
        QString(),          // ColCheckbox — 由 QCheckBox 覆盖
        QStringLiteral("序号"),
        QStringLiteral("文件名"),
        QStringLiteral("采集设备"),
        QStringLiteral("起始频率(MHz)"),
        QStringLiteral("结束频率(MHz)"),
        QStringLiteral("RBW(kHz)"),
        QStringLiteral("开始时间"),
        QStringLiteral("结束时间"),
        QStringLiteral("总时长"),
        QStringLiteral("大小"),
        QStringLiteral("信号数"),
        QStringLiteral("告警数"),
        QStringLiteral("操作")
    });

    // 基本属性
    m_table->setAlternatingRowColors(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setDefaultAlignment(Qt::AlignCenter);
    m_table->setShowGrid(true);
    m_table->setFocusPolicy(Qt::NoFocus);

    // 行高 / 表头高度一致
    m_table->verticalHeader()->setDefaultSectionSize(
        DPR_INT(30 * scale, dpr, 20));
    m_table->horizontalHeader()->setFixedHeight(
        DPR_INT(30 * scale, dpr, 20));

    // 前 8 列固定宽度（DPR 缩放），最后一列由 stretchLastSection 自动填充
    for (int c = 0; c < ColumnCount - 1; ++c)
        m_table->setColumnWidth(c, DPR_INT(kBaseColW[c] * scale, dpr, 20));

    m_table->installEventFilter(this);

    m_cornerRadius = DPR_INT(16 * scale, dpr, 8);  // 圆角半径
    const int r16 = m_cornerRadius;                  // 样式表圆角值
    const int pad2 = DPR_INT(2  * scale, dpr, 1);   // 上下内边距
    const int pad4 = DPR_INT(4  * scale, dpr, 2);   // 左右内边距
    const int bdr1 = DPR_INT(1  * scale, dpr, 1);   // 边框 / 分隔线

    // ---- 从主题读取颜色 ----
    const auto &tm         = ThemeManager::instance();
    const QString bgCol     = tm.colorString("hqListBox.backgroundColor");
    const QString borderCol  = tm.colorString("hqListBox.borderColor");

    m_table->setItemDelegateForColumn(ColCheckbox, new HQCheckBoxDelegate(m_table));

    const QString altRowCol  = tm.colorString("hqListBox.alternateRowColor");
    const QString gridCol    = tm.colorString("hqListBox.gridlineColor");
    const QString textCol    = tm.colorString("hqListBox.textColor");
    const QString hdrBgCol   = tm.colorString("hqListBox.headerBgColor");
    const QString hdrTextCol = tm.colorString("hqListBox.headerTextColor");
    const QString hdrBdrCol  = tm.colorString("hqListBox.headerBorderColor");
    const QString hoverCol   = tm.colorString("hqListBox.hoverRowColor");
    const QString selBgCol   = tm.colorString("hqListBox.selectedBgColor");
    const QString selTextCol = tm.colorString("hqListBox.selectedTextColor");

    m_table->setStyleSheet(QStringLiteral(
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
    ).arg(bgCol, borderCol, altRowCol, gridCol, textCol)
     .arg(bdr1).arg(r16).arg(pad2).arg(pad4)
     .arg(hoverCol, selBgCol, selTextCol)
     .arg(hdrBgCol, hdrTextCol, hdrBdrCol));

    // ---- 全选复选框 ----
    m_checkAll = new HQCheckBox(this);
    // 与单元格复选框代理使用完全一致的尺寸计算方式（DPR缩放，min=0 不做下限约束）
    m_checkAll->setMinBoxSize(0);
    const int chkBoxSz = DPR_INT(16 * scale, dpr, 0);
    m_checkAll->setFixedSize(chkBoxSz, chkBoxSz);
    m_checkAll->setChecked(false);

    // 单击全选 → 同步所有行复选框
    connect(m_checkAll, &HQCheckBox::stateChanged, this, [this](int state) {
        const bool checked = (state == Qt::Checked);
        m_table->blockSignals(true);
        for (int i = 0; i < m_table->rowCount(); ++i) {
            auto *item = m_table->item(i, ColCheckbox);
            if (item) {
                item->setData(HQCheckBoxDelegate::CheckStateRole, checked ? Qt::Checked : Qt::Unchecked);
                qint64 fid = getRowFileId(i);
                if (fid != 0) {
                    if (checked)
                        m_checkedFileIds.insert(fid);
                    else
                        m_checkedFileIds.remove(fid);
                }
            }
        }
        m_table->blockSignals(false);
        setSelectedCount(m_checkedFileIds.size());
    });

    // 单个行复选框变化时同步全选状态、已选计数和跨页持久化
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (!item || item->column() != ColCheckbox)
            return;
        // 更新跨页勾选状态
        qint64 fid = getRowFileId(item->row());
        if (fid != 0) {
            if (item->data(HQCheckBoxDelegate::CheckStateRole).toInt() == Qt::Checked)
                m_checkedFileIds.insert(fid);
            else
                m_checkedFileIds.remove(fid);
        }
        bool allChecked = true;
        for (int i = 0; i < m_table->rowCount(); ++i) {
            auto *it = m_table->item(i, ColCheckbox);
            if (it) {
                if (it->data(HQCheckBoxDelegate::CheckStateRole).toInt() != Qt::Checked)
                    allChecked = false;
            }
        }
        m_checkAll->blockSignals(true);
        m_checkAll->setChecked(allChecked);
        m_checkAll->blockSignals(false);
        setSelectedCount(m_checkedFileIds.size());
    });

    // 复选框初始定位（等事件循环开始后执行）
    // QTimer::singleShot(0, this, [this]() { updateCheckAllPos(); });
    // 列拖动/缩放时更新位置
    connect(m_table->horizontalHeader(), &QHeaderView::sectionResized,
            this, [this](int logicalIndex, int, int) {
        if (logicalIndex == ColCheckbox)
            updateCheckAllPos();
    });
    // 表头几何变化时更新位置
    connect(m_table->horizontalHeader(), &QHeaderView::geometriesChanged,
            this, [this]() { updateCheckAllPos(); });

    const QString scHandle       = tm.colorString("scrollbar.handleColor");
    const QString scHandleHover  = tm.colorString("scrollbar.handleHoverColor");
    const QString scHandlePressed= tm.colorString("scrollbar.handlePressedColor");

    auto applyScStyle = [dpr, scale, &scHandle, &scHandleHover, &scHandlePressed](QScrollBar *sb) {
        const int sbW  = DPR_INT(8  * scale, dpr, 4);   // 滚动条宽度
        const int sbR  = DPR_INT(4  * scale, dpr, 2);   // 滑块圆角
        const int sbMH = DPR_INT(30 * scale, dpr, 16);  // 滑块最小尺寸

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

    // ---- 布局 ----
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0,0,0,0);
    layout->setSpacing(0);
    layout->addWidget(m_table);
    layout->setSpacing(DPR_INT(8 * scale, dpr, 1));

    // ---- 底部翻页栏 ----
    createBottomBar();
    layout->addWidget(m_bottomBar);

    // 表格外框圆角遮罩（裁剪 header + viewport 适配圆角边框）
    QPainterPath maskPath;
    maskPath.addRoundedRect(m_table->rect(), m_cornerRadius, m_cornerRadius);
    m_table->setMask(maskPath.toFillPolygon().toPolygon());
}

void HQRecordListBox::addRow(const QStringList &cells, const SpectrumFileInfo& info, qint64 fileId)
{
    m_allRowData.append(cells);
    m_allFileInfos.append(info);
    m_allFileIds.append(fileId);
}

void HQRecordListBox::insertRowAtFront(const QStringList &cells, const SpectrumFileInfo& info, qint64 fileId)
{
    m_allRowData.prepend(cells);
    m_allFileInfos.prepend(info);
    m_allFileIds.prepend(fileId);
    // 重排所有行的序号（prepend 后原有行 index 都 +1）
    for (int i = 0; i < m_allRowData.size(); ++i)
        m_allRowData[i][0] = QString::number(i + 1);
}

void HQRecordListBox::refreshPage()
{
    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

    // Save current page checkbox states before clearing
    for (int i = 0; i < m_table->rowCount(); ++i) {
        auto *item = m_table->item(i, ColCheckbox);
        if (item) {
            qint64 fid = getRowFileId(i);
            if (fid != 0) {
                if (item->data(HQCheckBoxDelegate::CheckStateRole).toInt() == Qt::Checked)
                    m_checkedFileIds.insert(fid);
                else
                    m_checkedFileIds.remove(fid);
            }
        }
    }

    m_table->setRowCount(0);

    int total = m_allRowData.size();
    if (total <= 0) return;

    int start = (m_currentPage - 1) * m_pageSize;
    int end = qMin(start + m_pageSize, total);
    if (start >= total) return;

    for (int idx = start; idx < end; ++idx)
    {
        const QStringList &cells = m_allRowData[idx];
        const SpectrumFileInfo &info = m_allFileInfos[idx];
        qint64 fileId = m_allFileIds[idx];

        const int row = m_table->rowCount();
        m_table->insertRow(row);

    // 第 0 列：复选框
    auto *checkItem = new QTableWidgetItem();
    checkItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    checkItem->setData(HQCheckBoxDelegate::CheckStateRole,
        m_checkedFileIds.contains(fileId) ? Qt::Checked : Qt::Unchecked);
    checkItem->setTextAlignment(Qt::AlignCenter);
    m_table->setItem(row, ColCheckbox, checkItem);

    // 第 1 ~ 12 列：数据列（ColIndex … ColAlertNum），ColOper 由按钮面板接管
    const int dataEnd = ColOper;   // 13
    const int dataCnt = cells.size();
    for (int c = ColIndex; c < dataEnd && (c - ColIndex) < dataCnt; ++c) {
        auto *item = new QTableWidgetItem(cells.at(c - ColIndex));
        item->setTextAlignment(Qt::AlignCenter);
        item->setToolTip(cells.at(c - ColIndex));
        if (c == ColFileName && fileId != 0)
            item->setData(Qt::UserRole, fileId);
        m_table->setItem(row, c, item);
    }
    for (int c = ColIndex + dataCnt; c < dataEnd; ++c) {
        auto *item = new QTableWidgetItem(QString());
        item->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(row, c, item);
    }

    // ColOper：4 个图标按钮，水平居中
    auto *panel = new QWidget();
    panel->setProperty("isOperPanel", true);
    panel->installEventFilter(this);
    auto *lay   = new QHBoxLayout(panel);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->setSpacing(8);

    struct { const char *icon; const char *tip; } btns[] = {
        { ":/recordplayback/info.png",   u8"详情" },
        { ":/recordplayback/play.png",   u8"播放" },
        { ":/recordplayback/export.png",  u8"导出" },
        { ":/recordplayback/delete.png",  u8"删除" },
    };
    lay->addStretch();
    QPushButton *detailBtn = nullptr;
    QPushButton *playBtn   = nullptr;
    QPushButton *expBtn    = nullptr;
    QPushButton *delBtn    = nullptr;
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
        else if (btnIdx == 1)
            playBtn = btn;
        else if (btnIdx == 2)
            expBtn = btn;
        else if (btnIdx == 3)
            delBtn = btn;
        ++btnIdx;
    }

    // 连接导出按钮：自动加入该行文件ID，无需手动勾选
    if (expBtn) {
        connect(expBtn, &QPushButton::clicked, this, [this, row]() {
            qint64 fid = getRowFileId(row);
            if (fid != 0) {
                bool wasChecked = m_checkedFileIds.contains(fid);
                m_checkedFileIds.insert(fid);
                emit exportClicked();
                if (!wasChecked)
                    m_checkedFileIds.remove(fid);
            }
        });
    }

    // 连接"详情"按钮：收集整行数据并通过信号发送（末位追加 file_id）
    if (detailBtn) {
        connect(detailBtn, &QPushButton::clicked, this, [this, row]() {
            QStringList data;
            for (int c = ColIndex; c < ColOper; ++c) {
                auto *item = m_table->item(row, c);
                data << (item ? item->text() : QString());
            }
            // 末位追加文件ID，供详情页删除使用
            qint64 fid = getRowFileId(row);
            data << QString::number(fid);
            emit detailClicked(data);
        });
    }

    // 连接"删除"按钮：发射行删除信号
    if (delBtn) {
        connect(delBtn, &QPushButton::clicked, this, [this, row]() {
            emit rowDeleteClicked(row);
        });
    }

    // 连接"播放"按钮：弹出 RecordDialog
    if (playBtn) {
        connect(playBtn, &QPushButton::clicked, this, [this, row, info]() {
            auto *dlg = new RecordDialog(info, m_signalPath, this);
            connect(this, &HQRecordListBox::dialogSignalDetail, dlg, &RecordDialog::dialogSignalDetail);
            emit dialogSignalDetailSearch(info.file_id);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            dlg->exec();
        });
    }
    lay->addStretch();
    m_table->setCellWidget(row, ColOper, panel);

    }
    setSelectedCount(m_checkedFileIds.size());
}

void HQRecordListBox::clear()
{
    m_table->setRowCount(0);
    m_allRowData.clear();
    m_allFileInfos.clear();
    m_allFileIds.clear();
    m_checkedFileIds.clear();
    m_checkAll->blockSignals(true);
    m_checkAll->setChecked(false);
    m_checkAll->blockSignals(false);
}

qint64 HQRecordListBox::getRowFileId(int row) const
{
    if (row < 0 || row >= m_table->rowCount()) return 0;
    auto *item = m_table->item(row, ColFileName);
    if (!item) return 0;
    return item->data(Qt::UserRole).toLongLong();
}

QVector<qint64> HQRecordListBox::getCheckedFileIds() const
{
    QVector<qint64> ids;
    ids.reserve(m_checkedFileIds.size());
    for (qint64 fid : m_checkedFileIds)
        ids.append(fid);
    return ids;
}

void HQRecordListBox::removeCheckedFileIds(const QSet<qint64>& ids)
{
    for (qint64 fid : ids)
        m_checkedFileIds.remove(fid);
    setSelectedCount(m_checkedFileIds.size());
}

QVector<QStringList> HQRecordListBox::getAllRowData() const
{
    QVector<QStringList> rows;
    rows.reserve(m_allRowData.size());
    for (int i = 0; i < m_allRowData.size(); ++i) {
        QStringList row = m_allRowData[i];
        // 追加 file_id
        row << QString::number(m_allFileIds[i]);
        rows.append(row);
    }
    return rows;
}

void HQRecordListBox::upsertRow(const QStringList &cells, const SpectrumFileInfo& info, qint64 fileId)
{
    m_realFileInfo = info;
    for (int i = 0; i < m_allFileIds.size(); ++i) {
        if (m_allFileIds[i] == fileId) {
            // 更新内部数据
            m_allRowData[i] = cells;
            m_allFileInfos[i] = info;
            // 直接更新当前页的表格文本（不重建UI，不影响用户交互）
            int start = (m_currentPage - 1) * m_pageSize;
            int end = qMin(start + m_pageSize, m_allRowData.size());
            if (i >= start && i < end) {
                int tableRow = i - start;
                int dataCnt = cells.size();
                for (int c = ColIndex; c < ColOper && (c - ColIndex) < dataCnt; ++c) {
                    auto *item = m_table->item(tableRow, c);
                    if (item) item->setText(cells.at(c - ColIndex));
                }
            }
            return;
        }
    }
    // 未找到，在最前面插入新行（最新文件排第一）
    insertRowAtFront(cells, info, fileId);
    // 更新分页信息
    m_totalCount = m_allRowData.size();
    m_totalLabel->setText(QStringLiteral("共 %1 条").arg(m_totalCount));
    m_totalPage = (m_totalCount + m_pageSize - 1) / m_pageSize;
    if (m_totalPage < 1) m_totalPage = 1;
    if (m_currentPage > m_totalPage) m_currentPage = m_totalPage;
    updatePageButtons();
    refreshPage();
}

bool HQRecordListBox::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_table && event->type() == QEvent::Resize) {
        QPainterPath path;
        path.addRoundedRect(m_table->rect(), m_cornerRadius, m_cornerRadius);
        m_table->setMask(path.toFillPolygon().toPolygon());
    }
    return QWidget::eventFilter(obj, event);
}

// ============================================================================
// 全选复选框定位
// ============================================================================

void HQRecordListBox::updateCheckAllPos()
{
    if (!m_checkAll || !m_table->horizontalHeader())
        return;

    const int x = m_table->horizontalHeader()->sectionViewportPosition(ColCheckbox);
    const int w = m_table->horizontalHeader()->sectionSize(ColCheckbox);
    const int h = m_table->horizontalHeader()->height();
    const int cw = m_checkAll->width();
    const int ch = m_checkAll->height();

    const int posX = x + qMax(0, (w - cw) / 2);
    const int posY = h > 0 ? qMax(0, (h - ch) / 2) : 0;
    m_checkAll->move(posX, posY);
}

// ============================================================================
// 底部翻页栏
// ============================================================================

void HQRecordListBox::createBottomBar()
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

    // ---- 左侧：统计 + 操作按钮 ----
    m_totalLabel = new QLabel(m_bottomBar);
    m_totalLabel->setTextFormat(Qt::RichText);
    m_totalLabel->setText(QStringLiteral("共 0 条"));
    m_totalLabel->setFixedHeight(barH);
    m_totalLabel->setAlignment(Qt::AlignVCenter);
    m_selectedLabel = new QLabel(m_bottomBar);
    m_selectedLabel->setTextFormat(Qt::RichText);
    m_selectedLabel->setText(QStringLiteral("已选择 <span style='color:#E85C21;'>0</span> 条"));
    m_selectedLabel->setFixedHeight(barH);
    m_selectedLabel->setAlignment(Qt::AlignVCenter);

    const int iconBtnH = DPR_INT(28 * scale, dpr, 22);
    const int iconBtnW = DPR_INT(79 * scale, dpr, 79);

    auto *expBtn = new HQToolButton(m_bottomBar);
    expBtn->setHQIcon(QIcon(QStringLiteral(":/recordplayback/export.png")), 12, 12);
    expBtn->setText(QStringLiteral("导出 "));
    expBtn->setFixedSize(iconBtnW, iconBtnH);
    expBtn->setHQRadius(6);
    m_exportBtn = expBtn;

    auto *delBtn = new HQToolButton(m_bottomBar);
    delBtn->setHQIcon(QIcon(QStringLiteral(":/recordplayback/delete.png")), 12, 12);
    delBtn->setText(QStringLiteral("删除 "));
    delBtn->setFixedSize(iconBtnW, iconBtnH);
    delBtn->setHQRadius(6);
    m_deleteBtn = delBtn;

    lay->addWidget(m_totalLabel);
    lay->addWidget(m_selectedLabel);
    lay->addWidget(m_exportBtn);
    lay->addWidget(m_deleteBtn);

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
        m_pageSize = m_pageSizeCombo->currentData().toInt();
        m_totalPage = (m_totalCount + m_pageSize - 1) / m_pageSize;
        if (m_totalPage < 1) m_totalPage = 1;
        if (m_currentPage > m_totalPage) m_currentPage = m_totalPage;
        updatePageButtons();
        refreshPage();
        emit pageSizeChanged(m_pageSize);
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
        }
        m_jumpInput->setText(QString());
    });

    // ---- 连接信号 ----
    connect(m_prevBtn, &QPushButton::clicked, this, [this]() {
        if (m_currentPage > 1) {
            --m_currentPage;
            updatePageButtons();
            refreshPage();
            emit pageChanged(m_currentPage);
        }
    });
    connect(m_nextBtn, &QPushButton::clicked, this, [this]() {
        if (m_currentPage < m_totalPage) {
            ++m_currentPage;
            updatePageButtons();
            refreshPage();
            emit pageChanged(m_currentPage);
        }
    });
    connect(m_exportBtn, &QPushButton::clicked,
            this, &HQRecordListBox::exportClicked);
    connect(m_deleteBtn, &QPushButton::clicked,
            this, &HQRecordListBox::deleteClicked);

    // ---- 样式 ----
    const QString bgCol    = tm.colorString("hqListBox.backgroundColor");
    const QString textCol  = tm.colorString("hqListBox.textColor");
    const QString bdrCol   = tm.colorString("hqListBox.borderColor");

    // 设置导出/清理按钮的主题文字色
    static_cast<HQToolButton *>(m_exportBtn)->setTextColor(QColor(Qt::white));
    static_cast<HQToolButton *>(m_deleteBtn)->setTextColor(QColor(Qt::white));

    m_totalLabel->setStyleSheet(QStringLiteral(
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
    ).arg(bgCol, bdrCol));

    updatePageButtons();
}

void HQRecordListBox::updatePageButtons()
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
    const auto &ss     = ScreenScale::instance();
    const auto &tm     = ThemeManager::instance();
    const int fsPx     = DPR_INT(12 * ss.scale(), ss.dpr(), 12);
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

// ============================================================================
// 接口实现
// ============================================================================

void HQRecordListBox::setTotalCount(int count)
{
    m_totalCount = qMax(count, m_allRowData.size());
    m_totalLabel->setText(QStringLiteral("共 %1 条").arg(m_totalCount));
}

void HQRecordListBox::setSelectedCount(int count)
{
    m_selectedCount = count;
    m_selectedLabel->setText(QStringLiteral("已选择 <span style='color:#E85C21;'>%1</span> 条").arg(count));
}

void HQRecordListBox::setCurrentPage(int page)
{
    if (page < 1)      page = 1;
    if (page > m_totalPage) page = m_totalPage;
    m_currentPage = page;
    updatePageButtons();
    refreshPage();
}

void HQRecordListBox::setTotalPage(int pages)
{
    m_totalPage = (pages < 1) ? 1 : pages;
    if (m_currentPage > m_totalPage)
        m_currentPage = m_totalPage;
    updatePageButtons();
}

void HQRecordListBox::openRecordDialog()
{
    if (m_realFileInfo.file_id != -1)
    {
        auto *dlg = new RecordDialog(m_realFileInfo, m_signalPath, this);
        connect(this, &HQRecordListBox::dialogSignalDetail, dlg, &RecordDialog::dialogSignalDetail);
        emit dialogSignalDetailSearch(m_realFileInfo.file_id);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->exec();
    }
}

void HQRecordListBox::setSignalPath(const QString& signalPath)
{
    m_signalPath = signalPath + "/spectrum_data"; // spectrum_data为固定存储后缀
}
