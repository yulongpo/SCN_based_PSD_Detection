#include "HQListBox.h"
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
#include <QTime>
#include <QPropertyAnimation>
#include <QMouseEvent>

#include "BaseDef.h"

// ============================================================================
// 列宽度基准值（构造时按 DPR 缩放）
// ============================================================================
static const int kBaseColW[HQListBox::ColumnCount] = {
    70,   // ID
    250,   // 中心频率（MHz）
    250,   // 带宽（Hz）
    120,   // 信号类型
    250,   // 告警等级
    250,  // 最近出现时间
    60    // 出现次数
};

HQListBox::HQListBox(QWidget *parent)
    : QWidget(parent)
{
    // ---- DPR 缩放参数 ----
    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

    // ---- 表格 ----
    m_table = new QTableWidget(0, ColumnCount, this);
    m_table->setObjectName(QStringLiteral("hqListBox"));
    m_table->setHorizontalHeaderLabels({
        QStringLiteral("ID"),
        QStringLiteral("中心频率(MHz)"),
        QStringLiteral("带宽(kHz)"),
        QStringLiteral("信号类型"),
        QStringLiteral("告警等级"),
        QStringLiteral("最近出现时间"),
        QStringLiteral("出现次数")
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
    m_table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

    // 行高 / 表头高度一致
    m_table->verticalHeader()->setDefaultSectionSize(
        DPR_INT(30 * scale, dpr, 20));
    m_table->horizontalHeader()->setFixedHeight(
        DPR_INT(30 * scale, dpr, 20));

    // 前 8 列固定宽度（DPR 缩放），最后一列由 stretchLastSection 自动填充
    for (int c = 0; c < ColumnCount - 1; ++c)
        m_table->setColumnWidth(c, DPR_INT(kBaseColW[c] * scale, dpr, 20));

    m_table->installEventFilter(this);
    m_table->viewport()->installEventFilter(this);   // 同时监听 viewport，用于截获右键双击事件

    // ---- 行双击事件（仅右键双击触发）：通过 eventFilter 截获 viewport 右键双击 ----
    // QTableWidget::cellDoubleClicked 仅响应左键双击，无法区分按键，故改用事件过滤器处理

    // ---- 行单击事件：提取检测对象数据并通过信号发送至频谱图，定位/高亮对应标记框 ----
    connect(m_table, &QTableWidget::cellClicked, this,
            [this](int row, int /*column*/) {
        // 获取该行 ColID 项中存储的原始检测对象数据
        auto *idItem = m_table->item(row, ColID);
        if (!idItem) return;

        const int64_t id         = idItem->data(Qt::UserRole).toLongLong();
        const qint64  freqStart  = idItem->data(Qt::UserRole + 2).toLongLong();
        const qint64  freqStop   = idItem->data(Qt::UserRole + 3).toLongLong();
        const int     alarmLevel = idItem->data(Qt::UserRole + 4).toInt();

        emit rowClickedForMark(id, freqStart, freqStop, alarmLevel);
    });

    // ---- 表格样式（Fusion 确保 border-radius 正确渲染） ----
    m_table->setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    // ---- 告警等级列自定义绘制 ----
    auto *alertDelegate = new HQRowDelegate(m_table);
    alertDelegate->initPixmaps(dpr, scale);
    m_table->setItemDelegateForColumn(HQListBox::ColAlertLevel, alertDelegate);

    m_cornerRadius = DPR_INT(16 * scale, dpr, 8);  // 圆角半径
    const int r16 = m_cornerRadius;                  // 样式表圆角值
    const int pad2 = DPR_INT(2  * scale, dpr, 1);   // 上下内边距
    const int pad4 = DPR_INT(4  * scale, dpr, 2);   // 左右内边距
    const int bdr1 = DPR_INT(1  * scale, dpr, 1);   // 边框 / 分隔线

    // 表格字体大小：基准 9pt × 屏幕缩放系数，适配不同分辨率/DPI
    const int fontSizePt = DPR_INT(12 * scale, dpr, 6);
    const QString fontSizeStr = QString::number(fontSizePt) + QStringLiteral("px");

    // ---- 从主题读取颜色 ----
    const auto &tm         = ThemeManager::instance();
    const QString bgCol     = tm.colorString("hqListBox.backgroundColor");
    const QString borderCol  = tm.colorString("hqListBox.borderColor");
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
        "  font-size: %16;"
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
        "  font-size: %16;"
        "  padding: %8px %9px;"
        "  border: none;"
        "  border-bottom: %6px solid %15;"
        "  border-right: %6px solid %15;"
        "}"
        "QHeaderView::section:last {"
        "  border-right: none;"
        "}"
    ).arg(bgCol, borderCol, altRowCol, gridCol, textCol)
     .arg(bdr1).arg(r16).arg(pad2).arg(pad4)
     .arg(hoverCol, selBgCol, selTextCol)
     .arg(hdrBgCol, hdrTextCol, hdrBdrCol)
     .arg(fontSizeStr));

    // ---- 滚动条（Fusion + QSS） ----
    const QString scHandle       = tm.colorString("scrollbar.handleColor");
    const QString scHandleHover  = tm.colorString("scrollbar.handleHoverColor");
    const QString scHandlePressed= tm.colorString("scrollbar.handlePressedColor");

    auto applyScStyle = [dpr, scale, &scHandle, &scHandleHover, &scHandlePressed](QScrollBar *sb) {
        const int sbW  = DPR_INT(8  * scale, dpr, 4);   // 滚动条宽度
        const int sbR  = DPR_INT(4  * scale, dpr, 2);   // 滑块圆角
        const int sbMH = DPR_INT(30 * scale, dpr, 16);  // 滑块最小尺寸

        sb->setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
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
    layout->setContentsMargins(DPR_INT(12 * scale, dpr, 6),
                               DPR_INT(12 * scale, dpr, 6),
                               DPR_INT(12 * scale, dpr, 6),
                               DPR_INT(12 * scale, dpr, 6));
    layout->setSpacing(0);
    layout->addWidget(m_table);

    // 表格外框圆角遮罩（裁剪 header + viewport 适配圆角边框）
    QPainterPath maskPath;
    maskPath.addRoundedRect(m_table->rect(), m_cornerRadius, m_cornerRadius);
    m_table->setMask(maskPath.toFillPolygon().toPolygon());
}

void HQListBox::addRow(HQSigMF::DetectionObject* obj, int count)
{
    // ---- 查重：同一轨迹 ID 原位更新，频率/带宽随 Tracker 的当前结果变化 ----
    for (int r = 0; r < m_table->rowCount(); ++r) {
        auto *idItem = m_table->item(r, ColID);
        if (!idItem) continue;
        if (idItem->data(Qt::UserRole).toLongLong() != obj->id) continue;

        idItem->setData(Qt::UserRole + 1, obj->t_start_ns);
        idItem->setData(Qt::UserRole + 2, static_cast<qint64>(obj->f_start_hz));
        idItem->setData(Qt::UserRole + 3, static_cast<qint64>(obj->f_end_hz));
        idItem->setData(Qt::UserRole + 4, obj->alarm_level);

        if (auto *freqItem = m_table->item(r, ColFreq))
            freqItem->setText(QString::number(
                (obj->f_start_hz + obj->f_end_hz) / 2000000.0, 'f', 6));
        if (auto *bandwidthItem = m_table->item(r, ColBW))
            bandwidthItem->setText(QString::number(
                (obj->f_end_hz - obj->f_start_hz) / 1000.0, 'f', 3));
        if (auto *sigTypeItem = m_table->item(r, ColSigType))
            sigTypeItem->setText(obj->carry_type == 0
                ? QStringLiteral("常在") : QStringLiteral("突发"));

        if (auto *alertItem = m_table->item(r, ColAlertLevel)) {
            if (obj->alarm_level == 2) {
                alertItem->setText(QStringLiteral("严重"));
                alertItem->setData(AlertLevelModeRole, static_cast<int>(AlertSevere));
            } else if (obj->alarm_level == 1) {
                alertItem->setText(QStringLiteral("一般"));
                alertItem->setData(AlertLevelModeRole, static_cast<int>(AlertGeneral));
            } else {
                alertItem->setText(QStringLiteral("正常"));
                alertItem->setData(AlertLevelModeRole, static_cast<int>(AlertNormal));
            }
        }

        if (auto *timeItem = m_table->item(r, ColLastTime)) {
            const qint64 epochMs = static_cast<qint64>(obj->t_end_ns / 1e6);
            timeItem->setText(QDateTime::fromMSecsSinceEpoch(epochMs).toString(QStringLiteral("HH:mm:ss.zzz")));
            timeItem->setData(Qt::UserRole, epochMs);
        }
        repositionRow(r);
        return;
    }

    // ---- 无匹配，添加新行 ----
    const int row = m_table->rowCount();
    m_table->insertRow(row);

    // ID（存储 id 和原始频率数据到自定义角色，供后续查重和标记框联动使用）
    auto *item = new QTableWidgetItem(QString::number(obj->id));
    item->setTextAlignment(Qt::AlignCenter);
    item->setData(Qt::UserRole,     static_cast<qint64>(obj->id));
    item->setData(Qt::UserRole + 1, obj->t_start_ns);    // int64_t → QVariant(int64_t)
    item->setData(Qt::UserRole + 2, static_cast<qint64>(obj->f_start_hz));  ///< 起始频率（Hz），供标记框联动
    item->setData(Qt::UserRole + 3, static_cast<qint64>(obj->f_end_hz));    ///< 截止频率（Hz），供标记框联动
    item->setData(Qt::UserRole + 4, obj->alarm_level);                     ///< 告警等级，供标记框联动
    m_table->setItem(row, ColID, item);
    // 频率 MHz
    item = new QTableWidgetItem(QString::number((obj->f_start_hz + obj->f_end_hz) / 2000000.0, 'f', 6));
    item->setTextAlignment(Qt::AlignCenter);
    m_table->setItem(row, ColFreq, item);
    // 带宽 KHz
    item = new QTableWidgetItem(QString::number((obj->f_end_hz - obj->f_start_hz) / 1000.0, 'f', 3));
    item->setTextAlignment(Qt::AlignCenter);
    m_table->setItem(row, ColBW, item);
    // 信号类型
    item = new QTableWidgetItem(obj->carry_type == 0 ? QStringLiteral("常在") : QStringLiteral("突发"));
    item->setTextAlignment(Qt::AlignCenter);
    m_table->setItem(row, ColSigType, item);
    // 告警等级（alarm_level=1 → 一般，=2 → 严重，其他不设置）
    if (m_processNum.find(obj->id) == m_processNum.end()) emit addSignalNum(0, 1);
    if (obj->alarm_level == 1) {
        item = new QTableWidgetItem(QStringLiteral("一般"));
        item->setTextAlignment(Qt::AlignCenter);
        item->setData(AlertLevelModeRole, static_cast<int>(AlertGeneral));
        m_table->setItem(row, ColAlertLevel, item);
        if (m_processNum.find(obj->id) == m_processNum.end()) emit addSignalNum(1, 1);
    } else if (obj->alarm_level == 2) {
        item = new QTableWidgetItem(QStringLiteral("严重"));
        item->setTextAlignment(Qt::AlignCenter);
        item->setData(AlertLevelModeRole, static_cast<int>(AlertSevere));
        m_table->setItem(row, ColAlertLevel, item);
        if (m_processNum.find(obj->id) == m_processNum.end()) emit addSignalNum(2, 1);
    } else
    {
        item = new QTableWidgetItem(QStringLiteral("正常"));
        item->setTextAlignment(Qt::AlignCenter);
        item->setData(AlertLevelModeRole, static_cast<int>(AlertNormal));
        m_table->setItem(row, ColAlertLevel, item);
    }
    if (m_processNum.find(obj->id) == m_processNum.end()) m_processNum.insert(obj->id);// 避免因中期丢失后在后期又出现的标记
    // 其他 alarm_level 值不创建告警等级列项（单元格为空）
    // 最近出现时间（格式 HH:mm:ss.zzz）
    {
        const qint64 epochMs = static_cast<qint64>(obj->t_end_ns / 1e6);
        item = new QTableWidgetItem(QDateTime::fromMSecsSinceEpoch(epochMs).toString(QStringLiteral("HH:mm:ss.zzz")));
        item->setTextAlignment(Qt::AlignCenter);
        // 存储 epoch 毫秒时间戳，供过期清理时使用
        item->setData(Qt::UserRole, epochMs);
        m_table->setItem(row, ColLastTime, item);
    }
    // 出现次数
    item = new QTableWidgetItem(QString::number(count));
    item->setTextAlignment(Qt::AlignCenter);
    m_table->setItem(row, ColCount, item);

    // 新行已插入，将其向上冒泡到正确排序位置（增量重排）
    repositionRow(row);
}

bool HQListBox::rowKeyGreater(int r1, int r2) const
{
    // ---- 读取两行告警等级排序值（3=严重 > 2=一般 > 1=正常） ----
    int rank1 = AlertNormal, rank2 = AlertNormal;
    if (auto *ai = m_table->item(r1, ColAlertLevel)) {
        const int mode = ai->data(AlertLevelModeRole).toInt();
        if (mode == AlertSevere)       rank1 = 3;   // 严重
        else if (mode == AlertGeneral) rank1 = 2;   // 一般
    }
    if (auto *ai = m_table->item(r2, ColAlertLevel)) {
        const int mode = ai->data(AlertLevelModeRole).toInt();
        if (mode == AlertSevere)       rank2 = 3;   // 严重
        else if (mode == AlertGeneral) rank2 = 2;   // 一般
    }

    // 主排序：告警等级从大到小（严重 > 一般 > 正常）
    if (rank1 != rank2)
        return rank1 > rank2;

    // 次排序：最近出现时间（epoch 毫秒）从大到小，越新越靠前
    qint64 t1 = 0, t2 = 0;
    if (auto *ti = m_table->item(r1, ColLastTime))
        t1 = ti->data(Qt::UserRole).toLongLong();
    if (auto *ti = m_table->item(r2, ColLastTime))
        t2 = ti->data(Qt::UserRole).toLongLong();
    return t1 > t2;
}

void HQListBox::swapTableRows(int a, int b)
{
    // 逐列交换两行持有的项指针（takeItem 后原位置变空，再回填给对方）
    for (int c = 0; c < ColumnCount; ++c) {
        QTableWidgetItem *ia = m_table->takeItem(a, c);
        QTableWidgetItem *ib = m_table->takeItem(b, c);
        m_table->setItem(a, c, ib);
        m_table->setItem(b, c, ia);
    }
}

void HQListBox::repositionRow(int srcRow)
{
    // 单行向上冒泡：只要当前行排序键严格大于上一行就交换，否则停止
    // 与 std::stable_sort 语义一致：排序键相同的行保持原有相对顺序
    // 相比全表排序+重建，只移动真正需要调整的一行，避免每帧全表重建卡顿
    for (int r = srcRow; r > 0; --r) {
        if (rowKeyGreater(r, r - 1))
            swapTableRows(r, r - 1);
        else
            break;
    }
}

void HQListBox::clear()
{
    m_table->setRowCount(0);
    m_processNum.clear();
}

void HQListBox::removeOldRows(int64_t lastFrameTimeMs)
{
    // 从后向前遍历，避免删除行后索引错乱
    for (int r = m_table->rowCount() - 1; r >= 0; --r) {
        auto *timeItem = m_table->item(r, ColLastTime);
        if (!timeItem) {
            // 该行没有"最近出现时间"，视为过期，删除之
            m_table->removeRow(r);
            continue;
        }

        // 从 UserRole 中读取存储的 epoch 毫秒时间戳
        const qint64 rowTimeMs = timeItem->data(Qt::UserRole).toLongLong();

        // 如果该行的最近出现时间早于时频图最后一帧的时间，说明已过期，删除该行
        if (rowTimeMs < lastFrameTimeMs) {
            m_table->removeRow(r);
        }
    }
}

void HQListBox::scrollToRowById(int64_t id)
{
    for (int r = 0; r < m_table->rowCount(); ++r) {
        auto *idItem = m_table->item(r, ColID);
        if (!idItem) continue;
        if (idItem->data(Qt::UserRole).toLongLong() != id) continue;

        QScrollBar *vBar = m_table->verticalScrollBar();
        const int startVal = vBar->value();   // 在 selectRow/scrollToItem 之前捕获

        // 让 Qt 自己算出正确的居中位置，不自己算（r*rowH 在不等高/QSS下不准）
        m_table->setUpdatesEnabled(false);
        m_table->scrollToItem(idItem, QAbstractItemView::PositionAtCenter);
        const int targetVal = vBar->value();  // Qt 算出的精确目标值
        vBar->setValue(startVal);             // 瞬间复位（updates 屏蔽，不可见）
        m_table->setUpdatesEnabled(true);

        if (startVal == targetVal)
        {
            m_table->selectRow(r);
            return;
        }

        auto *anim = new QPropertyAnimation(vBar, "value", this);
        anim->setDuration(300);
        anim->setEasingCurve(QEasingCurve::OutCubic);
        anim->setStartValue(startVal);
        anim->setEndValue(targetVal);

        // 动画完成后再选中该行（selectRow 会触发内部滚动，不能提前调用）
        connect(anim, &QPropertyAnimation::finished, this, [this, r, anim]() {
            m_table->selectRow(r);
            anim->deleteLater();
        });
        anim->start();
        return;
    }
}

void HQListBox::setDbRecord(bool flag)
{
    m_dbRecord = flag;
}

bool HQListBox::eventFilter(QObject *obj, QEvent *event)
{
    // 原有：表格 resize 时更新圆角遮罩
    if (obj == m_table && event->type() == QEvent::Resize) {
        QPainterPath path;
        path.addRoundedRect(m_table->rect(), m_cornerRadius, m_cornerRadius);
        m_table->setMask(path.toFillPolygon().toPolygon());
    }

    // 右键双击检测：拦截 viewport 的 MouseButtonDblClick 事件
    // QTableWidget::cellDoubleClicked 仅响应左键双击，无法区分按键
    // 通过事件过滤器截获右键双击，替代 cellDoubleClicked 信号
    if (obj == m_table->viewport() && event->type() == QEvent::MouseButtonDblClick) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::RightButton) {
            // 将 viewport 坐标映射到表格坐标，获取双击位置的行
            QPoint tablePos = m_table->viewport()->mapTo(m_table, mouseEvent->pos());
            QTableWidgetItem *item = m_table->itemAt(tablePos);
            if (item && m_dbRecord) {
                emit rowDoubleClicked();
            }
        }
    }

    return QWidget::eventFilter(obj, event);
}

// ============================================================================
// HQRowDelegate 实现
// ============================================================================

void HQRowDelegate::initPixmaps(qreal dpr, double scale)
{
    const int logical = DPR_INT(12 * scale, dpr, 8);
    const int phys    = qRound(logical * dpr);
    auto scalePix = [phys, dpr](QPixmap &p) {
        if (p.size() != QSize(phys, phys) || p.devicePixelRatioF() != dpr) {
            p = p.scaled(phys, phys, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            p.setDevicePixelRatio(dpr);
        }
    };

    // ---- "正常"图标：程序生成绿色实心圆 ----
    m_normalPix = QPixmap(phys, phys);
    m_normalPix.fill(Qt::transparent);
    {
        QPainter p(&m_normalPix);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(QColor(46, 160, 67));   // 绿色
        p.setPen(Qt::NoPen);
        p.drawEllipse(1, 1, phys - 2, phys - 2);
    }
    m_normalPix.setDevicePixelRatio(dpr);
    // ---- "一般告警"图标：程序生成黄色实心圆 ----
    m_generalPix = QPixmap(phys, phys);
    m_generalPix.fill(Qt::transparent);
    {
        QPainter p(&m_generalPix);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(QColor(255, 186, 0));   //黄色
        p.setPen(Qt::NoPen);
        p.drawEllipse(1, 1, phys - 2, phys - 2);
    }
    m_generalPix.setDevicePixelRatio(dpr);
    // ---- "严重告警"图标：程序生成绿色实心圆 ----
    m_severePix = QPixmap(phys, phys);
    m_severePix.fill(Qt::transparent);
    {
        QPainter p(&m_severePix);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(QColor(230, 62, 62));   // 红色
        p.setPen(Qt::NoPen);
        p.drawEllipse(1, 1, phys - 2, phys - 2);
    }
    m_severePix.setDevicePixelRatio(dpr);

    // scalePix(m_generalPix);
    // scalePix(m_severePix);
}

void HQRowDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                           const QModelIndex &index) const
{
    // 告警等级列特殊绘制
    const int mode = index.data(AlertLevelModeRole).toInt();
    if (mode == HQListBox::AlertNormal || mode == HQListBox::AlertGeneral || mode == HQListBox::AlertSevere) {
        paintAlertCell(painter, option, index, mode);
        return;
    }

    QStyledItemDelegate::paint(painter, option, index);
}

void HQRowDelegate::paintAlertCell(QPainter *painter, const QStyleOptionViewItem &option,
                                    const QModelIndex &index, int mode) const
{
    // const QString origText = index.data(Qt::DisplayRole).toString();
    // if (origText.isEmpty())
    //     return;

    // 根据模式选择颜色、图标和标签
    QColor  color;
    const QPixmap *pixPtr = nullptr;
    QString label;
    if (mode == HQListBox::AlertSevere) {
        color  = QColor(230, 62, 62);        // 红色
        pixPtr = &m_severePix;
        label  = QStringLiteral("严重");
    } else if (mode == HQListBox::AlertGeneral) {
        color  = QColor(255, 186, 0);        // 黄色
        pixPtr = &m_generalPix;
        label  = QStringLiteral("一般");
    } else {
        color  = QColor(46, 160, 67);        // 绿色
        pixPtr = &m_normalPix;
        label  = QStringLiteral("正常");
    }

    if (!pixPtr || pixPtr->isNull())
        return;

    const QPixmap &pix = *pixPtr;

    // 只画选中背景面板，不画文字
    QStyleOptionViewItem bgOpt = option;
    initStyleOption(&bgOpt, index);
    bgOpt.text.clear();
    bgOpt.features &= ~QStyleOptionViewItem::HasDisplay;
    if (const QWidget *w = option.widget)
        w->style()->drawPrimitive(QStyle::PE_PanelItemViewItem, &bgOpt, painter, w);

    // 计算布局：[图标 "一般/严重" 边框] + 间距 + [原文(表格色)]
    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

    const int iconSize = DPR_INT(12 * scale, dpr, 8);   // 图标尺寸
    const int spacing  = DPR_INT(4  * scale, dpr, 2);   // 内部图文间距
    const int gap      = DPR_INT(6  * scale, dpr, 3);   // badge 与原文间距
    const int padH     = DPR_INT(6  * scale, dpr, 3);   // 水平内边距
    const int bdrR     = DPR_INT(3  * scale, dpr, 1);   // 矩形圆角
    const int bdrW     = DPR_INT(1  * scale, dpr, 1);   // 边框宽

    const QRect cellRect = option.rect;
    QFontMetrics fm(option.font);
    const int labelWidth  = fm.horizontalAdvance(label);
    // const int origWidth   = fm.horizontalAdvance(origText);
    const int badgeW      = iconSize + spacing + labelWidth + padH * 2;
    // const int totalW      = badgeW + gap + origWidth;
    const int totalW      = badgeW + gap;
    const int startX      = cellRect.left() + (cellRect.width() - totalW) / 2;
    const int h           = iconSize;
    const int y           = cellRect.top() + (cellRect.height() - h) / 2;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    // 设置 painter 字体与表格一致，确保 drawText 的文字大小和样式匹配
    // option.font 包含了 QSS 样式表中 font-size 等属性的计算结果
    painter->setFont(option.font);

    // 矩形边框（包裹 图标 + "一般/严重"）
    QPen pen(color, bdrW);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawRoundedRect(startX, y - DPR_INT(3 * scale, dpr, 0), badgeW, h + DPR_INT(6 * scale, dpr, 0), bdrR, bdrR);

    // 图标
    const int iconX = startX + padH;
    painter->drawPixmap(iconX, y, iconSize, iconSize, pix);

    // "一般"或"严重"（告警色）
    const int labelX = iconX + iconSize + spacing;
    painter->setPen(color);
    painter->drawText(labelX, y, badgeW - padH - (labelX - startX), h,
                      Qt::AlignLeft | Qt::AlignVCenter, label);

    // 原文（表格色）
    // const int origX = startX + badgeW + gap;
    // painter->setPen(ThemeManager::instance().color("hqListBox.textColor"));
    // painter->drawText(origX, y, origWidth, h,
    //                   Qt::AlignLeft | Qt::AlignVCenter, origText);

    painter->restore();
}
