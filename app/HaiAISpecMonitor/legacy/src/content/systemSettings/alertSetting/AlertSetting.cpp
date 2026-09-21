#include "AlertSetting.h"
#include "AlertRoleDialog.h"
#include "WhiteRoleDialog.h"

#include "comm/CommonMacros.h"
#include "comm/ScreenScale.h"
#include "comm/ThemeManager.h"
#include "comm/FontManager.h"
#include "widgets/HQSwitch.h"
#include "widgets/ToastWidget.h"
#include <climits>

#include <QPainter>
#include <QPainterPath>
#include <QShowEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QHeaderView>
#include <QScrollBar>
#include <QStyleFactory>
#include <QStyleOptionViewItem>

AlertSetting::AlertSetting(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, true);
    initUI();
}

void AlertSetting::setSignalAlarmRules(SignalAlarmRules rules)
{
    m_table->setRowCount(0);

    for (int32_t i = 0; i < rules.num; i++)
    {
        const auto& rule = rules.item[i];

        double freqStartMHz = rule.bgn_freq / 1000000.0;
        double freqEndMHz   = rule.end_freq / 1000000.0;
        double bwMaxkHz     = (rule.sig_max_bw > 0 && rule.sig_max_bw < INT32_MAX / 2)
                                ? rule.sig_max_bw / 1000.0 : 0.0;
        double bwMinkHz     = rule.sig_min_bw > 0 ? rule.sig_min_bw / 1000.0 : 0.0;

        QStringList cells;
        for (int col = 0; col < ColumnCount; ++col)
            cells << QString();
        cells[ColIndex]      = QString::number(i + 1);
        cells[ColName]       = QString::fromUtf8(rule.name);
        cells[ColFreqStart]  = QString::number(freqStartMHz, 'f', 3);
        cells[ColFreqEnd]    = QString::number(freqEndMHz, 'f', 3);
        cells[ColSigType]    = (rule.carry_type == 0) ? QStringLiteral("常在") : QStringLiteral("突发");
        cells[ColBWMax]      = (bwMaxkHz > 0.0) ? QString::number(bwMaxkHz, 'f', 1) : QStringLiteral("--");
        cells[ColBWMin]      = (bwMinkHz > 0.0) ? QString::number(bwMinkHz, 'f', 1) : QStringLiteral("--");
        cells[ColRemark]     = QString::fromUtf8(rule.note);

        int uiLevel = 0;
        if      (rule.alarm_level == 1) uiLevel = 2;
        else if (rule.alarm_level == 2) uiLevel = 3;

        addRow(cells, rule.rule_id, rule.enable == 1, uiLevel);
    }
    updateGeometry();
}

void AlertSetting::setSignalWhiteLists(SignalWhitelists whiteLists)
{
    m_whiteTable->setRowCount(0);

    for (int32_t i = 0; i < whiteLists.num; i++)
    {
        const auto& item = whiteLists.item[i];

        double freqStartMHz = item.bgn_freq / 1000000.0;
        double freqEndMHz   = item.end_freq / 1000000.0;

        QStringList cells;
        for (int col = 0; col < WhiteColumnCount; ++col)
            cells << QString();
        cells[WhiteColIndex]     = QString::number(i + 1);
        cells[WhiteColName]      = QString::fromUtf8(item.name);
        cells[WhiteColFreqStart] = QString::number(freqStartMHz, 'f', 3);
        cells[WhiteColFreqEnd]   = QString::number(freqEndMHz, 'f', 3);
        cells[WhiteColRemark]    = QString::fromUtf8(item.note);

        addWhiteRow(cells, item.id, item.enable == 1);
    }
    updateGeometry();
}

void AlertSetting::alarmRuleOpResp(const SignalAlarmRuleOpResp& response)
{
    if (m_pendingRefresh > 0) {
        --m_pendingRefresh;
        return;
    }
    if (response.success == 0)
        ToastWidget::instance()->showInfo(QStringLiteral("操作成功"));
    else
        ToastWidget::instance()->showError(QStringLiteral("操作失败"));
}

void AlertSetting::whiteListsOpResp(const SignalWhitelistOpResp& response)
{
    if (m_pendingRefresh > 0) {
        --m_pendingRefresh;
        return;
    }
    if (response.success == 0)
        ToastWidget::instance()->showInfo(QStringLiteral("操作成功"));
    else
        ToastWidget::instance()->showError(QStringLiteral("操作失败"));
}

void AlertSetting::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // 页面显示时同时刷新告警规则与白名单，发出两个刷新请求，故计数为 2
    m_pendingRefresh = 2;
    SignalAlarmRuleOpReq req;
    req.op_type = 3;
    emit alarmRuleOpReq(req);

    SignalWhitelistOpReq whiteReq;
    whiteReq.op_type = 3;
    emit whiteListsOpReq(whiteReq);
}

void AlertSetting::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

    const qreal w = width();
    const qreal h = height();

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
}

void AlertSetting::initUI()
{
    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

    const int padInt   = DPR_INT(20 * scale, dpr, 10);
    const int spacing  = DPR_INT(8 * scale, dpr, 4);
    const int titleFs  = DPR_INT(15 * scale, dpr, 11);
    const int labelFs  = DPR_INT(13 * scale, dpr, 9);
    const QColor titleColor(0xDE, 0xEF, 0xFF);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // ============================================================
    // 标题栏："告警规则" + "新增规则" 按钮
    // ============================================================
    auto *titleRow = new QHBoxLayout;
    titleRow->setContentsMargins(padInt, spacing, padInt, spacing);

    auto *titleLabel = new QLabel(QStringLiteral("告警规则"), this);
    titleLabel->setFont(FontManager::instance().font(titleFs, QFont::Bold));
    titleLabel->setStyleSheet(QString("color: %1; padding: 0; margin: 0;").arg(titleColor.name()));
    titleRow->addWidget(titleLabel);

    titleRow->addStretch();

    int btnRadius = DPR_INT(6 * scale, dpr, 0);
    int btnFontSize = DPR_INT(13 * scale, dpr, 0);
    m_addBtn = new QPushButton(QStringLiteral("+新增规则"), this);
    m_addBtn->setCursor(Qt::PointingHandCursor);
    m_addBtn->setFixedSize(DPR_INT(109 * scale, dpr, 0), DPR_INT(28 * scale, dpr, 0));
    QString titleStyle = QStringLiteral(
        "QPushButton {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0,"
        "    stop:0 rgba(10, 140, 254, 90),"
        "    stop:1 rgba(10, 140, 254, 0));"
        "  border: 1px solid rgb(101, 129, 157);"
        "  border-radius: %1px;"
        "  color: white;"
        "  font-size: %2px;"
        "  padding: 0;"
        "}"
        "QPushButton:hover {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0,"
        "    stop:0 rgba(10, 140, 254, 120),"
        "    stop:1 rgba(10, 140, 254, 30));"
        "  border: 1px solid rgb(151, 179, 207);"
        "  color: #CCE8FF;"
        "}"
        "QPushButton:pressed {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0,"
        "    stop:0 rgba(10, 140, 254, 60),"
        "    stop:1 rgba(10, 140, 254, 0));"
        "  border: 1px solid rgb(81, 109, 137);"
        "  color: #A0D0FF;"
        "  padding-top: 1px;"
        "  padding-left: 1px;"
        "}").arg(btnRadius).arg(btnFontSize);
    m_addBtn->setStyleSheet(titleStyle);
    titleRow->addWidget(m_addBtn);

    mainLayout->addLayout(titleRow);

    // ============================================================
    // 规则列表表格（11 列）
    // ============================================================
    auto *tableContainer = new QWidget(this);
    auto *tableLayout = new QVBoxLayout(tableContainer);
    tableLayout->setContentsMargins(padInt, 0, padInt, padInt);
    tableLayout->setSpacing(0);

    m_table = new QTableWidget(0, ColumnCount, tableContainer);
    m_table->setObjectName(QStringLiteral("alertRuleTable"));
    m_table->setHorizontalHeaderLabels({
        QStringLiteral("序号"),
        QStringLiteral("名称"),
        QStringLiteral("起始频率(MHz)"),
        QStringLiteral("截止频率(MHz)"),
        QStringLiteral("信号类型"),
        QStringLiteral("告警等级"),
        QStringLiteral("信号最大带宽(kHz)"),
        QStringLiteral("信号最小带宽(kHz)"),
        QStringLiteral("备注"),
        QStringLiteral("启用状态"),
        QStringLiteral("操作")
    });

    // 基本属性
    m_table->setAlternatingRowColors(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);

    m_table->horizontalHeader()->setDefaultAlignment(Qt::AlignCenter);
    m_table->setShowGrid(true);
    m_table->setFocusPolicy(Qt::NoFocus);

    // 行高 / 表头高度
    m_table->verticalHeader()->setDefaultSectionSize(DPR_INT(32 * scale, dpr, 0));
    m_table->horizontalHeader()->setFixedHeight(DPR_INT(32 * scale, dpr, 0));

    // 列宽：第一列固定最小，其余列等宽平分
    {
        auto *hdr = m_table->horizontalHeader();
        hdr->setSectionResizeMode(ColIndex, QHeaderView::Fixed);
        m_table->setColumnWidth(ColIndex, DPR_INT(40 * scale, dpr, 20));
        for (int c = ColName; c < ColumnCount; ++c)
            hdr->setSectionResizeMode(c, QHeaderView::Stretch);
    }

    // 表格纵向自动扩充，行数多时出现滚动条
    m_table->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // 告警等级列使用自定义委托（不显示冗余文字）
    auto *alertDelegate = new AlertLevelDelegate(m_table);
    {
        const auto &s = ScreenScale::instance();
        alertDelegate->initPixmaps(s.dpr(), s.scale());
    }
    m_table->setItemDelegateForColumn(ColAlertLevel, alertDelegate);

    // ---- 从主题读取颜色，应用 HQListBox 风格 QSS ----
    const auto &tm           = ThemeManager::instance();
    const QString bgCol       = tm.colorString("hqListBox.backgroundColor");
    const QString borderCol   = tm.colorString("hqListBox.borderColor");
    const QString altRowCol   = tm.colorString("hqListBox.alternateRowColor");
    const QString gridCol     = tm.colorString("hqListBox.gridlineColor");
    const QString textCol     = tm.colorString("hqListBox.textColor");
    const QString hdrBgCol    = tm.colorString("hqListBox.headerBgColor");
    const QString hdrTextCol  = tm.colorString("hqListBox.headerTextColor");
    const QString hdrBdrCol   = tm.colorString("hqListBox.headerBorderColor");
    const QString hoverCol    = tm.colorString("hqListBox.hoverRowColor");
    const QString selBgCol    = tm.colorString("hqListBox.selectedBgColor");
    const QString selTextCol  = tm.colorString("hqListBox.selectedTextColor");

    const int r16 = DPR_INT(16 * scale, dpr, 8);
    const int pad2 = DPR_INT(2  * scale, dpr, 1);
    const int pad4 = DPR_INT(4  * scale, dpr, 2);
    const int bdr1 = DPR_INT(1  * scale, dpr, 1);

    QString tableStyle = QStringLiteral(
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
    ).arg(bgCol, borderCol, altRowCol, gridCol, textCol)
     .arg(bdr1).arg(r16).arg(pad2).arg(pad4)
     .arg(hoverCol, selBgCol, selTextCol)
     .arg(hdrBgCol, hdrTextCol, hdrBdrCol);
    m_table->setStyleSheet(tableStyle);

    // 滚动条样式
    const QString scHandle        = tm.colorString("scrollbar.handleColor");
    const QString scHandleHover   = tm.colorString("scrollbar.handleHoverColor");
    const QString scHandlePressed = tm.colorString("scrollbar.handlePressedColor");

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

    tableLayout->addWidget(m_table);
    mainLayout->addWidget(tableContainer, 1);

    // ============================================================
    // 标题栏："白名单规则" + "新增规则" 按钮
    // ============================================================
    titleRow = new QHBoxLayout;
    titleRow->setContentsMargins(padInt, spacing, padInt, spacing);

    titleLabel = new QLabel(QStringLiteral("白名单规则"), this);
    titleLabel->setFont(FontManager::instance().font(titleFs, QFont::Bold));
    titleLabel->setStyleSheet(QString("color: %1; padding: 0; margin: 0;").arg(titleColor.name()));
    titleRow->addWidget(titleLabel);

    titleRow->addStretch();

    btnRadius = DPR_INT(6 * scale, dpr, 0);
    btnFontSize = DPR_INT(13 * scale, dpr, 0);
    m_whiteAddBtn = new QPushButton(QStringLiteral("+新增规则"), this);
    m_whiteAddBtn->setCursor(Qt::PointingHandCursor);
    m_whiteAddBtn->setFixedSize(DPR_INT(109 * scale, dpr, 0), DPR_INT(28 * scale, dpr, 0));
    m_whiteAddBtn->setStyleSheet(titleStyle);
    titleRow->addWidget(m_whiteAddBtn);

    mainLayout->addLayout(titleRow);

    // ============================================================
    // 白名单规则列表表格（7 列）
    // ============================================================
    tableContainer = new QWidget(this);
    tableLayout = new QVBoxLayout(tableContainer);
    tableLayout->setContentsMargins(padInt, 0, padInt, padInt);
    tableLayout->setSpacing(0);

    m_whiteTable = new QTableWidget(0, WhiteColumnCount, tableContainer);
    m_whiteTable->setObjectName(QStringLiteral("whiteRuleTable"));
    m_whiteTable->setHorizontalHeaderLabels({
        QStringLiteral("序号"),
        QStringLiteral("名称"),
        QStringLiteral("起始频率(MHz)"),
        QStringLiteral("截止频率(MHz)"),
        QStringLiteral("备注"),
        QStringLiteral("启用状态"),
        QStringLiteral("操作")
    });

    // 基本属性
    m_whiteTable->setAlternatingRowColors(true);
    m_whiteTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_whiteTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_whiteTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_whiteTable->verticalHeader()->setVisible(false);

    m_whiteTable->horizontalHeader()->setDefaultAlignment(Qt::AlignCenter);
    m_whiteTable->setShowGrid(true);
    m_whiteTable->setFocusPolicy(Qt::NoFocus);

    // 行高 / 表头高度
    m_whiteTable->verticalHeader()->setDefaultSectionSize(DPR_INT(32 * scale, dpr, 0));
    m_whiteTable->horizontalHeader()->setFixedHeight(DPR_INT(32 * scale, dpr, 0));

    // 列宽：第一列固定最小，其余列等宽平分
    {
        auto *hdr = m_whiteTable->horizontalHeader();
        hdr->setSectionResizeMode(WhiteColIndex, QHeaderView::Fixed);
        m_whiteTable->setColumnWidth(WhiteColIndex, DPR_INT(40 * scale, dpr, 20));
        for (int c = WhiteColIndex; c < WhiteColumnCount; ++c)
            hdr->setSectionResizeMode(c, QHeaderView::Stretch);
    }

    // 表格纵向自动扩充，行数多时出现滚动条
    m_whiteTable->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_whiteTable->setStyleSheet(tableStyle);
    applyScStyle(m_whiteTable->verticalScrollBar());
    applyScStyle(m_whiteTable->horizontalScrollBar());

    tableLayout->addWidget(m_whiteTable);
    mainLayout->addWidget(tableContainer, 1);

    // 新增规则：弹出对话框 → 构建请求 → emit → 等待后端 reportRules() 刷新
    connect(m_addBtn, &QPushButton::clicked, this, [this]() {
        AlertRoleDialog dlg(this);
        if (dlg.exec() != QDialog::Accepted)
            return;

        SignalAlarmRuleOpReq req;
        req.op_type = 0;

        QString name = dlg.ruleName();
        strncpy(req.rule.name, name.toUtf8().constData(), sizeof(req.rule.name) - 1);
        req.rule.name[sizeof(req.rule.name) - 1] = '\0';
        req.rule.enable = 1;

        req.rule.bgn_freq = static_cast<int64_t>(
            (dlg.freqStart().isEmpty() ? 0.0 : dlg.freqStart().toDouble()) * 1000000);
        req.rule.end_freq = static_cast<int64_t>(
            (dlg.freqEnd().isEmpty() ? 1000.0 : dlg.freqEnd().toDouble()) * 1000000);

        req.rule.carry_type = (dlg.sigType() == QStringLiteral("突发")) ? 1 : 0;

        int uiLevel = dlg.alertLevelMode();
        req.rule.alarm_level = (uiLevel == 3) ? 2 : 1;

        QString bwMaxText = dlg.bwMax();
        req.rule.sig_max_bw = (bwMaxText.isEmpty() || bwMaxText == QStringLiteral("--"))
            ? INT32_MAX : static_cast<int32_t>(bwMaxText.toDouble() * 1000);

        QString bwMinText = dlg.bwMin();
        req.rule.sig_min_bw = (bwMinText.isEmpty() || bwMinText == QStringLiteral("--"))
            ? 0 : static_cast<int32_t>(bwMinText.toDouble() * 1000);

        QString note = dlg.remark();
        strncpy(req.rule.note, note.toUtf8().constData(), sizeof(req.rule.note) - 1);
        req.rule.note[sizeof(req.rule.note) - 1] = '\0';

        emit alarmRuleOpReq(req);
    });

    connect(m_whiteAddBtn, &QPushButton::clicked, this, [this]() {
        WhiteRoleDialog dlg(this);
        if (dlg.exec() != QDialog::Accepted)
            return;

        SignalWhitelistOpReq req;
        req.op_type = 0;

        QString name = dlg.ruleName();
        strncpy(req.item.name, name.toUtf8().constData(), sizeof(req.item.name) - 1);
        req.item.name[sizeof(req.item.name) - 1] = '\0';
        req.item.enable = 1;

        req.item.bgn_freq = static_cast<int64_t>(
            (dlg.freqStart().isEmpty() ? 0.0 : dlg.freqStart().toDouble()) * 1000000);
        req.item.end_freq = static_cast<int64_t>(
            (dlg.freqEnd().isEmpty() ? 1000.0 : dlg.freqEnd().toDouble()) * 1000000);

        QString note = dlg.remark();
        strncpy(req.item.note, note.toUtf8().constData(), sizeof(req.item.note) - 1);
        req.item.note[sizeof(req.item.note) - 1] = '\0';

        emit whiteListsOpReq(req);
    });
}

void AlertSetting::addRow(const QStringList &cells, int64_t ruleId, bool enabled, int alertLevelMode)
{
    const int row = m_table->rowCount();
    m_table->insertRow(row);

    for (int c = 0; c < ColumnCount && c < cells.size(); ++c) {
        if (c == ColEnabled) continue;
        auto *item = new QTableWidgetItem(cells.at(c));
        item->setTextAlignment(Qt::AlignCenter);
        item->setData(RuleIdRole, static_cast<qint64>(ruleId));
        m_table->setItem(row, c, item);
    }

    if (auto *item = m_table->item(row, ColAlertLevel))
        item->setData(AlertLevelDelegate::AlertLvlRole, alertLevelMode);

    auto *swContainer = new QWidget(m_table);
    auto *swLayout = new QHBoxLayout(swContainer);
    swLayout->setContentsMargins(0, 0, 0, 0);
    swLayout->setAlignment(Qt::AlignCenter);
    auto *sw = new HQSwitch(QString(), swContainer);
    sw->setOn(enabled);
    connect(sw, &HQSwitch::toggled, this, [this, row](bool on) {
        int64_t rid = 0;
        if (auto *item = m_table->item(row, ColName))
            rid = item->data(RuleIdRole).toLongLong();
        if (rid <= 0) return;

        auto txt = [this](int r, int c) {
            return m_table->item(r, c) ? m_table->item(r, c)->text() : QString();
        };

        SignalAlarmRuleOpReq req;
        req.op_type = 2;
        req.rule.rule_id = rid;
        req.rule.enable = on ? 1 : 0;

        QString name = txt(row, ColName);
        strncpy(req.rule.name, name.toUtf8().constData(), sizeof(req.rule.name) - 1);
        req.rule.name[sizeof(req.rule.name) - 1] = '\0';

        req.rule.bgn_freq = static_cast<int64_t>(txt(row, ColFreqStart).toDouble() * 1000000);
        req.rule.end_freq = static_cast<int64_t>(txt(row, ColFreqEnd).toDouble() * 1000000);

        req.rule.carry_type = (txt(row, ColSigType) == QStringLiteral("突发")) ? 1 : 0;

        int uiLvl = 0;
        if (auto *item = m_table->item(row, ColAlertLevel))
            uiLvl = item->data(AlertLevelDelegate::AlertLvlRole).toInt();
        req.rule.alarm_level = (uiLvl == 3) ? 2 : 1;

        QString bwMax = txt(row, ColBWMax);
        req.rule.sig_max_bw = (bwMax.isEmpty() || bwMax == QStringLiteral("--"))
            ? INT32_MAX : static_cast<int32_t>(bwMax.toDouble() * 1000);

        QString bwMin = txt(row, ColBWMin);
        req.rule.sig_min_bw = (bwMin.isEmpty() || bwMin == QStringLiteral("--"))
            ? 0 : static_cast<int32_t>(bwMin.toDouble() * 1000);

        QString note = txt(row, ColRemark);
        strncpy(req.rule.note, note.toUtf8().constData(), sizeof(req.rule.note) - 1);
        req.rule.note[sizeof(req.rule.note) - 1] = '\0';

        emit alarmRuleOpReq(req);
    });
    swLayout->addWidget(sw);
    m_table->setCellWidget(row, ColEnabled, swContainer);

    setupOperationRow(row);
    updateGeometry();
}

void AlertSetting::setupOperationRow(int row)
{
    auto *opWidget = new QWidget(m_table);
    auto *opLayout = new QHBoxLayout(opWidget);
    opLayout->setContentsMargins(0, 0, 0, 0);
    opLayout->setSpacing(4);
    opLayout->setAlignment(Qt::AlignCenter);

    auto *editBtn = new QPushButton(QStringLiteral("编辑"), opWidget);
    editBtn->setFlat(true);
    editBtn->setCursor(Qt::PointingHandCursor);
    editBtn->setStyleSheet(QStringLiteral(
        "QPushButton { color: #0A8CFE; background: transparent; border: none;"
        "  font-size: 12px; }"
        "QPushButton:hover { color: #3DA6FF; }"));

    auto *delBtn = new QPushButton(QStringLiteral("删除"), opWidget);
    delBtn->setFlat(true);
    delBtn->setCursor(Qt::PointingHandCursor);
    delBtn->setStyleSheet(QStringLiteral(
        "QPushButton { color: #FF4444; background: transparent; border: none;"
        "  font-size: 12px; }"
        "QPushButton:hover { color: #FF6666; }"));

    opLayout->addWidget(editBtn);
    opLayout->addWidget(delBtn);

    connect(editBtn, &QPushButton::clicked, this, [this]() {
        auto *btn = qobject_cast<QPushButton *>(sender());
        if (!btn) return;
        int row = -1;
        for (int r = 0; r < m_table->rowCount(); ++r) {
            if (m_table->cellWidget(r, ColOperation) != btn->parentWidget())
                continue;
            row = r;
            break;
        }
        if (row < 0) return;

        int64_t ruleId = 0;
        if (auto *item = m_table->item(row, ColName))
            ruleId = item->data(RuleIdRole).toLongLong();
        if (ruleId <= 0) return;

        auto itemText = [this](int r, int c) {
            return m_table->item(r, c) ? m_table->item(r, c)->text() : QString();
        };
        const QString name     = itemText(row, ColName);
        const QString freqStr  = itemText(row, ColFreqStart);
        const QString freqEnd  = itemText(row, ColFreqEnd);
        const QString sigType  = itemText(row, ColSigType);
        const QString bwMax    = itemText(row, ColBWMax);
        const QString bwMin    = itemText(row, ColBWMin);
        const QString remark   = itemText(row, ColRemark);

        int lvlMode = 2;
        if (auto *item = m_table->item(row, ColAlertLevel))
            lvlMode = item->data(AlertLevelDelegate::AlertLvlRole).toInt();

        bool enabled = false;
        if (auto *container = m_table->cellWidget(row, ColEnabled))
            if (auto *sw = container->findChild<HQSwitch*>())
                enabled = sw->isOn();

        AlertRoleDialog dlg(this);
        dlg.setEditData(name, freqStr, freqEnd, sigType, lvlMode, bwMax, bwMin, remark);
        if (dlg.exec() != QDialog::Accepted)
            return;

        SignalAlarmRuleOpReq req;
        req.op_type = 2;
        req.rule.rule_id = ruleId;
        req.rule.enable = enabled ? 1 : 0;

        QString newName = dlg.ruleName();
        strncpy(req.rule.name, newName.toUtf8().constData(), sizeof(req.rule.name) - 1);
        req.rule.name[sizeof(req.rule.name) - 1] = '\0';

        req.rule.bgn_freq = static_cast<int64_t>(dlg.freqStart().toDouble() * 1000000);
        req.rule.end_freq = static_cast<int64_t>(dlg.freqEnd().toDouble() * 1000000);

        req.rule.carry_type = (dlg.sigType() == QStringLiteral("突发")) ? 1 : 0;

        int newUiLevel = dlg.alertLevelMode();
        req.rule.alarm_level = (newUiLevel == 3) ? 2 : 1;

        QString newBwMax = dlg.bwMax();
        req.rule.sig_max_bw = (newBwMax.isEmpty() || newBwMax == QStringLiteral("--"))
            ? INT32_MAX : static_cast<int32_t>(newBwMax.toDouble() * 1000);

        QString newBwMin = dlg.bwMin();
        req.rule.sig_min_bw = (newBwMin.isEmpty() || newBwMin == QStringLiteral("--"))
            ? 0 : static_cast<int32_t>(newBwMin.toDouble() * 1000);

        QString newNote = dlg.remark();
        strncpy(req.rule.note, newNote.toUtf8().constData(), sizeof(req.rule.note) - 1);
        req.rule.note[sizeof(req.rule.note) - 1] = '\0';

        emit alarmRuleOpReq(req);
    });

    connect(delBtn, &QPushButton::clicked, this, [this]() {
        auto *btn = qobject_cast<QPushButton*>(sender());
        if (!btn) return;
        for (int r = 0; r < m_table->rowCount(); ++r) {
            if (m_table->cellWidget(r, ColOperation) != btn->parentWidget())
                continue;

            int64_t ruleId = 0;
            if (auto *item = m_table->item(r, ColName))
                ruleId = item->data(RuleIdRole).toLongLong();

            if (ruleId <= 0) {
                m_table->removeRow(r);
                for (int rr = 0; rr < m_table->rowCount(); ++rr)
                    if (auto *item = m_table->item(rr, ColIndex))
                        item->setText(QString::number(rr + 1));
                updateGeometry();
                return;
            }

            SignalAlarmRuleOpReq req;
            req.op_type = 1;
            req.rule.rule_id = ruleId;
            emit alarmRuleOpReq(req);
            return;
        }
    });

    m_table->setCellWidget(row, ColOperation, opWidget);
}

void AlertSetting::addWhiteRow(const QStringList &cells, int64_t whiteId, bool enabled)
{
    const int row = m_whiteTable->rowCount();
    m_whiteTable->insertRow(row);

    for (int c = 0; c < WhiteColumnCount && c < cells.size(); ++c) {
        if (c == WhiteColEnabled) continue;
        auto *item = new QTableWidgetItem(cells.at(c));
        item->setTextAlignment(Qt::AlignCenter);
        item->setData(RuleIdRole, static_cast<qint64>(whiteId));
        m_whiteTable->setItem(row, c, item);
    }

    // 启用状态列：放置 HQSwitch 开关
    auto *swContainer = new QWidget(m_whiteTable);
    auto *swLayout = new QHBoxLayout(swContainer);
    swLayout->setContentsMargins(0, 0, 0, 0);
    swLayout->setAlignment(Qt::AlignCenter);
    auto *sw = new HQSwitch(QString(), swContainer);
    sw->setOn(enabled);
    connect(sw, &HQSwitch::toggled, this, [this, row](bool on) {
        int64_t whiteId = 0;
        if (auto *item = m_whiteTable->item(row, WhiteColName))
            whiteId = item->data(RuleIdRole).toLongLong();
        if (whiteId <= 0) return;

        auto txt = [this](int r, int c) {
            return m_whiteTable->item(r, c) ? m_whiteTable->item(r, c)->text() : QString();
        };

        SignalWhitelistOpReq req;
        req.op_type = 2;
        req.item.id = whiteId;
        req.item.enable = on ? 1 : 0;

        QString name = txt(row, WhiteColName);
        strncpy(req.item.name, name.toUtf8().constData(), sizeof(req.item.name) - 1);
        req.item.name[sizeof(req.item.name) - 1] = '\0';

        req.item.bgn_freq = static_cast<int64_t>(txt(row, WhiteColFreqStart).toDouble() * 1000000);
        req.item.end_freq = static_cast<int64_t>(txt(row, WhiteColFreqEnd).toDouble() * 1000000);

        QString note = txt(row, WhiteColRemark);
        strncpy(req.item.note, note.toUtf8().constData(), sizeof(req.item.note) - 1);
        req.item.note[sizeof(req.item.note) - 1] = '\0';

        emit whiteListsOpReq(req);
    });
    swLayout->addWidget(sw);
    m_whiteTable->setCellWidget(row, WhiteColEnabled, swContainer);

    setupWhiteOperationRow(row);
    updateGeometry();
}

void AlertSetting::setupWhiteOperationRow(int row)
{
    auto *opWidget = new QWidget(m_whiteTable);
    auto *opLayout = new QHBoxLayout(opWidget);
    opLayout->setContentsMargins(0, 0, 0, 0);
    opLayout->setSpacing(4);
    opLayout->setAlignment(Qt::AlignCenter);

    auto *editBtn = new QPushButton(QStringLiteral("编辑"), opWidget);
    editBtn->setFlat(true);
    editBtn->setCursor(Qt::PointingHandCursor);
    editBtn->setStyleSheet(QStringLiteral(
        "QPushButton { color: #0A8CFE; background: transparent; border: none;"
        "  font-size: 12px; }"
        "QPushButton:hover { color: #3DA6FF; }"));

    auto *delBtn = new QPushButton(QStringLiteral("删除"), opWidget);
    delBtn->setFlat(true);
    delBtn->setCursor(Qt::PointingHandCursor);
    delBtn->setStyleSheet(QStringLiteral(
        "QPushButton { color: #FF4444; background: transparent; border: none;"
        "  font-size: 12px; }"
        "QPushButton:hover { color: #FF6666; }"));

    opLayout->addWidget(editBtn);
    opLayout->addWidget(delBtn);

    // 编辑：弹窗回填当前数据 → 组装更新请求
    connect(editBtn, &QPushButton::clicked, this, [this]() {
        auto *btn = qobject_cast<QPushButton *>(sender());
        if (!btn) return;
        int row = -1;
        for (int r = 0; r < m_whiteTable->rowCount(); ++r) {
            if (m_whiteTable->cellWidget(r, WhiteColOperation) != btn->parentWidget())
                continue;
            row = r;
            break;
        }
        if (row < 0) return;

        int64_t whiteId = 0;
        if (auto *item = m_whiteTable->item(row, WhiteColName))
            whiteId = item->data(RuleIdRole).toLongLong();
        if (whiteId <= 0) return;

        auto itemText = [this](int r, int c) {
            return m_whiteTable->item(r, c) ? m_whiteTable->item(r, c)->text() : QString();
        };
        const QString name     = itemText(row, WhiteColName);
        const QString freqStr  = itemText(row, WhiteColFreqStart);
        const QString freqEnd  = itemText(row, WhiteColFreqEnd);
        const QString remark   = itemText(row, WhiteColRemark);

        bool enabled = false;
        if (auto *container = m_whiteTable->cellWidget(row, WhiteColEnabled))
            if (auto *sw = container->findChild<HQSwitch*>())
                enabled = sw->isOn();

        WhiteRoleDialog dlg(this);
        dlg.setEditData(name, freqStr, freqEnd, remark);
        if (dlg.exec() != QDialog::Accepted)
            return;

        SignalWhitelistOpReq req;
        req.op_type = 2;
        req.item.id = whiteId;
        req.item.enable = enabled ? 1 : 0;

        QString newName = dlg.ruleName();
        strncpy(req.item.name, newName.toUtf8().constData(), sizeof(req.item.name) - 1);
        req.item.name[sizeof(req.item.name) - 1] = '\0';

        req.item.bgn_freq = static_cast<int64_t>(
            (dlg.freqStart().isEmpty() ? 0.0 : dlg.freqStart().toDouble()) * 1000000);
        req.item.end_freq = static_cast<int64_t>(
            (dlg.freqEnd().isEmpty() ? 1000.0 : dlg.freqEnd().toDouble()) * 1000000);

        QString newNote = dlg.remark();
        strncpy(req.item.note, newNote.toUtf8().constData(), sizeof(req.item.note) - 1);
        req.item.note[sizeof(req.item.note) - 1] = '\0';

        emit whiteListsOpReq(req);
    });

    // 删除：无ID直接本地移除，否则发送删除请求
    connect(delBtn, &QPushButton::clicked, this, [this]() {
        auto *btn = qobject_cast<QPushButton*>(sender());
        if (!btn) return;
        for (int r = 0; r < m_whiteTable->rowCount(); ++r) {
            if (m_whiteTable->cellWidget(r, WhiteColOperation) != btn->parentWidget())
                continue;

            int64_t whiteId = 0;
            if (auto *item = m_whiteTable->item(r, WhiteColName))
                whiteId = item->data(RuleIdRole).toLongLong();

            if (whiteId <= 0) {
                m_whiteTable->removeRow(r);
                for (int rr = 0; rr < m_whiteTable->rowCount(); ++rr)
                    if (auto *item = m_whiteTable->item(rr, WhiteColIndex))
                        item->setText(QString::number(rr + 1));
                updateGeometry();
                return;
            }

            SignalWhitelistOpReq req;
            req.op_type = 1;
            req.item.id = whiteId;
            emit whiteListsOpReq(req);
            return;
        }
    });

    m_whiteTable->setCellWidget(row, WhiteColOperation, opWidget);
}

void AlertLevelDelegate::initPixmaps(qreal dpr, double scale)
{
    const int logical = DPR_INT(12 * scale, dpr, 8);
    const int phys    = qRound(logical * dpr);
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
}

void AlertLevelDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                                const QModelIndex &index) const
{
    const int mode = index.data(AlertLvlRole).toInt();
    if (mode == 2 || mode == 3) {
        paintBadge(painter, option, index, mode);
        return;
    }
    QStyledItemDelegate::paint(painter, option, index);
}

void AlertLevelDelegate::paintBadge(QPainter *painter, const QStyleOptionViewItem &option,
                                     const QModelIndex &index, int mode) const
{
    // 选中背景
    QStyleOptionViewItem bgOpt = option;
    initStyleOption(&bgOpt, index);
    bgOpt.text.clear();
    bgOpt.features &= ~QStyleOptionViewItem::HasDisplay;
    if (const QWidget *w = option.widget)
        w->style()->drawPrimitive(QStyle::PE_PanelItemViewItem, &bgOpt, painter, w);

    const bool isSevere = (mode == 3);
    const QColor color  = isSevere ? QColor(230, 62, 62) : QColor(255, 186, 0);
    const QPixmap &pix  = isSevere ? m_severePix : m_generalPix;
    const QString label = isSevere ? QStringLiteral("严重") : QStringLiteral("一般");

    if (pix.isNull()) return;

    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

    const int iconSize = DPR_INT(12 * scale, dpr, 8);
    const int spacing  = DPR_INT(4  * scale, dpr, 2);
    const int padH     = DPR_INT(6  * scale, dpr, 3);
    const int bdrR     = DPR_INT(3  * scale, dpr, 1);
    const int bdrW     = DPR_INT(1  * scale, dpr, 1);

    const QRect cellRect = option.rect;
    QFontMetrics fm(option.font);
    const int labelWidth = fm.horizontalAdvance(label);
    const int badgeW     = iconSize + spacing + labelWidth + padH * 2;
    const int startX     = cellRect.left() + (cellRect.width() - badgeW) / 2;
    const int h          = iconSize;
    const int y          = cellRect.top() + (cellRect.height() - h) / 2;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    // 边框
    painter->setPen(QPen(color, bdrW));
    painter->setBrush(Qt::NoBrush);
    painter->drawRoundedRect(startX, y - DPR_INT(3 * scale, dpr, 0), badgeW, h + DPR_INT(6 * scale, dpr, 0), bdrR, bdrR);

    // 图标
    painter->drawPixmap(startX + padH, y, iconSize, iconSize, pix);

    // "一般" / "严重"
    const int textX = startX + padH + iconSize + spacing;
    painter->setPen(color);
    painter->drawText(textX, y, badgeW - (textX - startX), h,
                      Qt::AlignLeft | Qt::AlignVCenter, label);

    painter->restore();
}
