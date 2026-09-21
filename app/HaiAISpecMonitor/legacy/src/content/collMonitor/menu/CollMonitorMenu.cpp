#include "CollMonitorMenu.h"
#include "AlarmPanel.h"
#include "comm/CommonMacros.h"
#include "comm/ScreenScale.h"
#include "comm/ThemeManager.h"
#include <QEvent>
#include <QFile>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

#include "comm/FontManager.h"

CollMonitorMenu::CollMonitorMenu(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, true);
    m_startBtnStatus = false;
    m_pauseBtnStatus = false;
    m_updatingFreq   = false;
    m_isTestData = false;
    m_isDbClickSource = false;

    initUI();
    initConnections();

    // 主题切换时同步更新标签颜色与统计面板颜色
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, [this](bool /*night*/) {
        updateCollectMonitorUI();
    });
}

void CollMonitorMenu::setParams(ParamTable& params)
{
    // 解析出菜单的参数
    for (auto it = params.begin(); it != params.end(); it++)
    {
        if (it->first == "SpectrumSource")
        {
            setMenuParams(it->second);
            break;
        }
    }
}

void CollMonitorMenu::slotResponse(haiq::GuiResponseData& responseData)
{
    if (responseData._result == haiq::Result::Success)
    {
        if (responseData._cmdType == haiq::CMDType::CMD_RESUME && responseData._uuid == 1) // 下方请求时附带的标识 uuid
        {
            m_pauseBtn->setEnabled(true);
            m_startBtnStatus = true;
            m_startBtn->setText(QStringLiteral("停止监测"));
            m_startBtn->setButtonStyle(HQButton::Error);
            m_startBtn->setIconType(HQButton::Stop);
            // ToastWidget::instance()->showInfo("开始监测成功");
            setContentEnable(false);
            if (m_pauseBtnStatus)
            {
                // 如果是暂停查看状态就还原
                onPauseBtn_clicked();
            }
            else
            {
                emit signalListDbRecord(false);
            }
        }
        else if (responseData._cmdType == haiq::CMDType::CMD_RESUME && responseData._uuid == 2)
        {
            m_pauseBtnStatus = false;
            m_pauseBtn->setText(QStringLiteral("暂停查看"));
            m_pauseBtn->setButtonStyle(HQButton::Error);
            m_pauseBtn->setIconType(HQButton::Pause);
            // ToastWidget::instance()->showInfo("继续查看成功");
        }
        else
        {
            switch (responseData._cmdType)
            {
            case haiq::CMDType::CMD_PAUSE:
                m_pauseBtnStatus = true;
                m_pauseBtn->setText(QStringLiteral("继续查看"));
                m_pauseBtn->setButtonStyle(HQButton::Primary_normal);
                m_pauseBtn->setIconType(HQButton::Start);
                // ToastWidget::instance()->showInfo("暂停查看成功");
                break;
            case haiq::CMDType::CMD_PARAM:
                m_newParams = m_params;
                // ToastWidget::instance()->showInfo("参数更新成功");
                break;
            case haiq::CMDType::CMD_STOP:
                m_pauseBtn->setEnabled(false);
                m_startBtnStatus = false;
                m_startBtn->setText(QStringLiteral("开始监测"));
                m_startBtn->setButtonStyle(HQButton::Primary);
                m_startBtn->setIconType(HQButton::Start);
                // ToastWidget::instance()->showInfo("停止监测成功");
                setContentEnable(true);
                emit signalListDbRecord(true);
                break;
            default:
                break;
            }
        }
    }
    else
    {
        if (responseData._cmdType == haiq::CMDType::CMD_RESUME && responseData._uuid == 1)
        {
            ToastWidget::instance()->showError(u8"开始分析失败");
        }
        else if (responseData._cmdType == haiq::CMDType::CMD_RESUME && responseData._uuid == 2)
        {
            ToastWidget::instance()->showError(u8"继续查看失败");
        }
        else
        {
            switch (responseData._cmdType)
            {
            case haiq::CMDType::CMD_PAUSE:
                ToastWidget::instance()->showError(u8"暂停分析失败");
                break;
            case haiq::CMDType::CMD_PARAM:
                ToastWidget::instance()->showError(u8"下发参数更新失败");
                break;
            case haiq::CMDType::CMD_STOP:
                ToastWidget::instance()->showError(u8"停止分析失败");
                break;
            default:
                break;
            }
        }
    }
}

void CollMonitorMenu::addSignalNum(int type, int num)
{
    switch (type)
    {
    case 0:
        m_signalTotalNumLabel->setText(QString::number(m_signalTotalNumLabel->text().toInt() + num));
        break;
    case 1:
        m_generalAlarmNumLabel->setText(QString::number(m_generalAlarmNumLabel->text().toInt() + num));
        break;
    case 2:
        m_criticalAlertNumLabel->setText(QString::number(m_criticalAlertNumLabel->text().toInt() + num));
        break;
    default:
        break;
    }
}

void CollMonitorMenu::filePathChanged(const QString& filePath)
{
    if (m_isTestData == false) return;
    int64_t fc = 0;
    int64_t bw = 0;
    int64_t rbw = 0;
    int64_t level = 0;
    parseFilePath(filePath.toStdString(), fc, bw, rbw, level);
    m_frequencyEdit->setValue(fc);
    m_scanWidthEdit->setValue(bw);
    m_rbwEdit->setValue(rbw);
    m_refLevelSpin->setValue(level);
    emit signalMenuInfo(fc, bw, rbw);
}

void CollMonitorMenu::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

    // --- 绘制左侧面板（grid + 按钮）背景、边框和分割线 ---
    if (m_panel) {
        const QRect pRect = m_panel->geometry();
        const qreal panelRadius  = DPR_REAL(16.0 * scale, dpr);
        const qreal panelBorderW = DPR_REAL(1.5 * scale, dpr);
        const qreal halfPBW      = panelBorderW * 0.5;

        // 背景填充 9,13,24
        QPainterPath bgPath;
        bgPath.addRoundedRect(QRectF(pRect), panelRadius, panelRadius);
        painter.fillPath(bgPath, QColor(9, 13, 24));

        // 边框
        QPainterPath borderPath2;
        borderPath2.addRoundedRect(
            QRectF(pRect.left() + halfPBW, pRect.top() + halfPBW,
                   pRect.width() - panelBorderW, pRect.height() - panelBorderW),
            panelRadius - halfPBW, panelRadius - halfPBW);
        painter.setPen(QPen(ThemeManager::instance().color("CollMonitor.borderColor"), panelBorderW));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(borderPath2);

        // 分割线：在 startBtn 左侧，分隔 grid 和按钮
        if (m_startBtn && m_startBtn->isVisible()) {
            const int dividerX = m_startBtn->geometry().left() - DPR_REAL(12.0 * scale, dpr);
            const int margin   = DPR_INT(8 * scale, dpr, 2);
            painter.setPen(QPen(ThemeManager::instance().color("CollMonitor.borderColor"),
                                DPR_REAL(1.0 * scale, dpr)));
            painter.drawLine(dividerX, pRect.top() + margin,
                             dividerX, pRect.bottom() - margin);
        }

        // 分割线：在统计面板左侧，分隔按钮和三个标记（严重警告/一般警告/信号总数）
        if (m_pauseBtn && m_pauseBtn->isVisible()) {
            const int dividerX = m_pauseBtn->geometry().right() + DPR_REAL(12.0 * scale, dpr);
            const int margin   = DPR_INT(8 * scale, dpr, 2);
            painter.setPen(QPen(ThemeManager::instance().color("CollMonitor.borderColor"),
                                DPR_REAL(1.0 * scale, dpr)));
            painter.drawLine(dividerX, pRect.top() + margin,
                             dividerX, pRect.bottom() - margin);
        }
    }
}

void CollMonitorMenu::initUI()
{
    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

    // 内边距 12px（设计稿 2x 基准），转换为逻辑像素后保存
    const int padInt = static_cast<int>(DPR_REAL(12.0 * scale, dpr));
    const int btnHeight = DPR_INT(28 * scale, dpr, 4);

    this->setFixedHeight(DPR_INT(70 * scale, dpr, 0) + padInt);
    auto contentLayout = new QHBoxLayout(this);
    // 设置内边距，使子控件不会贴到自绘边框的边缘
    contentLayout->setContentsMargins(0, 0, 0, 0);

    QFont labelFont = FontManager::instance().font(DPR_INT(13 * scale, dpr, 9), QFont::Medium);
    labelFont.setWeight(QFont::Normal);
    labelFont.setStyle(QFont::StyleNormal);
    auto createLabel = [this, labelFont, scale, dpr](const QString& text)-> QLabel*
    {
        auto* label = new QLabel(this);
        label->setFont(labelFont);
        label->setText(text);
        label->setStyleSheet(
            QString("color: %1; background: transparent; border: none;")
                .arg(ThemeManager::instance().colorString("titleBar.collectMonitorColor")));
        m_collectMonitorLabels.append(label);
        QFontMetrics fm(labelFont);
        label->setFixedWidth(fm.width(text));
        return label;
    };

    // 左侧面板容器（grid + 按钮），背景和边框在 paintEvent 中绘制
    m_panel = new QWidget(this);
    m_panel->setAutoFillBackground(false);
    auto panelLayout = new QHBoxLayout(m_panel);
    panelLayout->setContentsMargins(padInt, padInt / 2, padInt, padInt / 2);
    panelLayout->setSpacing(padInt * 2);

    auto vLayout   = new QVBoxLayout();             // 左侧 grid
    auto hLayout   = new QHBoxLayout();             // 右侧按钮
    hLayout->setSpacing(padInt);

    panelLayout->addLayout(vLayout, 1);   // stretch=1，让左侧网格布局占用剩余空间
    panelLayout->addLayout(hLayout);
    contentLayout->addWidget(m_panel, 1);    // stretch=1 占用剩余宽度

    // 左侧布局（表格布局：2行×6列，不跨列，两行列对齐）
    auto gridLayout = new QGridLayout();
    gridLayout->setHorizontalSpacing(DPR_INT(16 * scale, dpr, 2));
    gridLayout->setVerticalSpacing(padInt / 2);
    gridLayout->setContentsMargins(0, 0, 0, 0);
    // 中心频率（第0行）
    m_frequencyLabel = createLabel(QStringLiteral("中心频率"));
    gridLayout->addWidget(m_frequencyLabel, 0, 0);
    auto *freqEdit = new HQFreqLineEdit(this);
    freqEdit->setPlaceholderText(QStringLiteral("请输入..."));
    freqEdit->setFixedHeight(btnHeight);
    freqEdit->setMinimumWidth(DPR_INT(190 * scale, dpr, 4));
    freqEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);   // 水平方向自动拉伸填充剩余空间
    freqEdit->setMaximumWidth(QWIDGETSIZE_MAX);                            // 释放 refreshMetrics() 中 setFixedSize 锁死的最大宽度
    m_frequencyEdit = freqEdit;
    gridLayout->addWidget(freqEdit, 0, 1);
    // 扫宽（第0行）
    m_scanWidthLabel = createLabel(QStringLiteral("扫宽"));
    gridLayout->addWidget(m_scanWidthLabel, 0, 2);
    auto *scanWidthEdit = new HQFreqLineEdit(this);
    scanWidthEdit->setPlaceholderText(QStringLiteral("请输入..."));
    scanWidthEdit->setFixedHeight(btnHeight);
    scanWidthEdit->setMinimumWidth(DPR_INT(190 * scale, dpr, 4));
    scanWidthEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);   // 水平方向自动拉伸填充剩余空间
    scanWidthEdit->setMaximumWidth(QWIDGETSIZE_MAX);                            // 释放 refreshMetrics() 中 setFixedSize 锁死的最大宽度
    m_scanWidthEdit = scanWidthEdit;
    gridLayout->addWidget(scanWidthEdit, 0, 3);
    // RBW（第0行）
    m_rbwLabel = createLabel(QStringLiteral("带宽分辨率"));
    gridLayout->addWidget(m_rbwLabel, 0, 4);
    auto *rbwEdit = new HQFreqLineEdit(this);
    rbwEdit->setPlaceholderText(QStringLiteral("请输入..."));
    rbwEdit->setFixedHeight(btnHeight);
    rbwEdit->setMinimumWidth(DPR_INT(190 * scale, dpr, 4));
    rbwEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);   // 水平方向自动拉伸填充剩余空间
    rbwEdit->setMaximumWidth(QWIDGETSIZE_MAX);                            // 释放 refreshMetrics() 中 setFixedSize 锁死的最大宽度
    m_rbwEdit = rbwEdit;
    gridLayout->addWidget(rbwEdit, 0, 5);

    // 文件路径（第0行，测试数据模式时显示，默认隐藏）
    m_filePathLabel = createLabel(QStringLiteral("文件路径"));
    gridLayout->addWidget(m_filePathLabel, 0, 0);
    m_filePathLabel->setVisible(false);

    m_filePathEdit = new HQFileLineEdit(this);
    m_filePathEdit->setSelectFile(true);
    m_filePathEdit->setPlaceholderText(QStringLiteral("请选择回放文件..."));
    m_filePathEdit->setFixedHeight(btnHeight);
    m_filePathEdit->setMinimumWidth(DPR_INT(190 * scale, dpr, 4));
    m_filePathEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_filePathEdit->setMaximumWidth(QWIDGETSIZE_MAX);
    gridLayout->addWidget(m_filePathEdit, 0, 1, 1, 5);   // 跨5列（列1~列5）
    m_filePathEdit->setVisible(false);

    // 起始频率（第1行）
    gridLayout->addWidget(createLabel(QStringLiteral("起始频率")), 1, 0);
    auto *startFreqEdit = new HQFreqLineEdit(this);
    startFreqEdit->setPlaceholderText(QStringLiteral("请输入..."));
    startFreqEdit->setFixedHeight(btnHeight);
    startFreqEdit->setMinimumWidth(DPR_INT(190 * scale, dpr, 4));
    startFreqEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);   // 水平方向自动拉伸填充剩余空间
    startFreqEdit->setMaximumWidth(QWIDGETSIZE_MAX);                            // 释放 refreshMetrics() 中 setFixedSize 锁死的最大宽度
    m_startFreqEdit = startFreqEdit;
    gridLayout->addWidget(startFreqEdit, 1, 1);
    // 终止频率（第1行）
    gridLayout->addWidget(createLabel(QStringLiteral("终止频率")), 1, 2);
    auto *endFreqEdit = new HQFreqLineEdit(this);
    endFreqEdit->setPlaceholderText(QStringLiteral("请输入..."));
    endFreqEdit->setFixedHeight(btnHeight);
    endFreqEdit->setMinimumWidth(DPR_INT(190 * scale, dpr, 4));
    endFreqEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);   // 水平方向自动拉伸填充剩余空间
    endFreqEdit->setMaximumWidth(QWIDGETSIZE_MAX);                            // 释放 refreshMetrics() 中 setFixedSize 锁死的最大宽度
    m_endFreqEdit = endFreqEdit;
    gridLayout->addWidget(endFreqEdit, 1, 3);
    // 参考电平（第1行）
    auto *hbox = new QHBoxLayout;
    hbox->setSpacing(0);
    hbox->setContentsMargins(0, 0, 0, 0);
    gridLayout->addWidget(createLabel(QStringLiteral("参考电平")), 1, 4);
    auto *refSpin = new HQSpinBox(this);
    refSpin->setDecimals(1);                       // 浮点模式，1位小数
    refSpin->setRange(-1000.0, 1000.0);
    refSpin->setDecimals(3);
    refSpin->setSingleStep(1);
    refSpin->setValue(0.0);
    refSpin->setFixedHeight(btnHeight);
    refSpin->setMinimumWidth(DPR_INT(120 * scale, dpr, 4));
    refSpin->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);   // 水平方向自动拉伸填充剩余空间
    refSpin->setMaximumWidth(QWIDGETSIZE_MAX);                            // 释放 refreshMetrics() 中 setFixedSize 锁死的最大宽度
    m_refLevelSpin = refSpin;
    m_refLevelSpinUnit = new HQCComboBox(this);
    m_refLevelSpinUnit->setPlaceholderText(QStringLiteral("选择设备"));
    m_refLevelSpinUnit->addItems(QStringList() << QStringLiteral("dBm"));
    m_refLevelSpinUnit->setCurrentIndex(0);
    m_refLevelSpinUnit->setFixedHeight(btnHeight);
    m_refLevelSpinUnit->setFixedWidth(DPR_INT(70 * scale, dpr, 4));
    hbox->addWidget(refSpin);
    hbox->addWidget(m_refLevelSpinUnit);
    // 将 spin+combo 整体放入第3列（不跨列，两行列对齐）
    gridLayout->addLayout(hbox, 1, 5);

    vLayout->addLayout(gridLayout, 1);   // stretch=1，让网格布局占满vLayout的剩余空间

    // 设置网格列拉伸：让输入框列（1, 3, 5）自动扩展以填充剩余宽度
    gridLayout->setColumnStretch(0, 0);   // 标签列不拉伸
    gridLayout->setColumnStretch(1, 1);   // 输入框列拉伸
    gridLayout->setColumnStretch(2, 0);   // 标签列不拉伸
    gridLayout->setColumnStretch(3, 1);   // 输入框列拉伸
    gridLayout->setColumnStretch(4, 0);   // 标签列不拉伸
    gridLayout->setColumnStretch(5, 1);   // 输入框列拉伸

    // 右侧按钮布局
    m_startBtn = new HQButton(this);
    m_startBtn->setText(QStringLiteral("开始监测"));
    m_startBtn->setButtonStyle(HQButton::Primary);
    m_startBtn->setIconType(HQButton::Start);
    m_startBtn->setFixedSize(DPR_INT(120 * scale, dpr, 40), btnHeight * 1.4);
    m_startBtn->setFont(FontManager::instance().font(DPR_INT(16 * scale, dpr, 0), QFont::Bold));
    connect(m_startBtn, &HQButton::clicked, this, &CollMonitorMenu::onStartBtn_clicked);
    // 监听开始按钮右键双击（打开文件源切换弹窗）
    m_startBtn->installEventFilter(this);

    m_pauseBtn = new HQButton(this);
    m_pauseBtn->setEnabled(false);
    m_pauseBtn->setText(QStringLiteral("暂停查看"));
    m_pauseBtn->setButtonStyle(HQButton::Error);
    m_pauseBtn->setIconType(HQButton::Pause);
    m_pauseBtn->setFixedSize(DPR_INT(120 * scale, dpr, 40), btnHeight * 1.4);
    m_pauseBtn->setFont(FontManager::instance().font(DPR_INT(16 * scale, dpr, 0), QFont::Bold));
    connect(m_pauseBtn, &HQButton::clicked, this, &CollMonitorMenu::onPauseBtn_clicked);

    hLayout->addWidget(m_startBtn);
    hLayout->addWidget(m_pauseBtn);

    // 创建三个统计项：严重警告 / 一般警告 / 信号总数
    {
        auto *panel = new AlarmPanel(
            QStringLiteral(":/collect/critical_alert.png"),
            QStringLiteral("严重警告"),
            QStringLiteral("collStatPanel.criticalAlertColor"), this);
        m_criticalAlertNumLabel = panel->numberLabel();
        panelLayout->addWidget(panel);
    }
    {
        auto *panel = new AlarmPanel(
            QStringLiteral(":/collect/general_alarm.png"),
            QStringLiteral("一般警告"),
            QStringLiteral("collStatPanel.generalAlarmColor"), this);
        m_generalAlarmNumLabel = panel->numberLabel();
        panelLayout->addWidget(panel);
    }
    {
        auto *panel = new AlarmPanel(
            QStringLiteral(":/collect/signal_total.png"),
            QStringLiteral("信号总数"),
            QStringLiteral("collStatPanel.signalTotalColor"), this);
        m_signalTotalNumLabel = panel->numberLabel();
        panelLayout->addWidget(panel);
    }
}

void CollMonitorMenu::initConnections()
{
    // ============================================================
    // 频率四联动：中心频率 / 扫宽 / 起始频率 / 终止频率
    // 任意一个输入框的值发生变化时，自动同步计算其余三个
    // 公式：start = fc - span/2,  end = fc + span/2
    //       fc = (start + end)/2, span = end - start
    // m_updatingFreq 互斥标志防止 setText 触发递归更新
    // ============================================================

    // 中心频率/扫宽变化 → 重新计算起始频率和终止频率
    auto syncFromFcSpan = [this](const QString &) {
        if (m_updatingFreq) return;
        m_updatingFreq = true;
        int64_t fc = m_frequencyEdit->value();
        int64_t span = m_scanWidthEdit->value();
        m_startFreqEdit->setValue(fc - span / 2.0);
        m_endFreqEdit->setValue(fc + span / 2.0);
        m_updatingFreq = false;
    };

    // 起始频率/终止频率变化 → 重新计算中心频率和扫宽
    auto syncFromStartEnd = [this](const QString &) {
        if (m_updatingFreq) return;
        m_updatingFreq = true;
        int64_t start = m_startFreqEdit->value();
        int64_t end   = m_endFreqEdit->value();
        m_frequencyEdit->setValue((start + end) / 2.0);
        m_scanWidthEdit->setValue(end - start);
        m_updatingFreq = false;
    };

    // 绑定信号：任一输入框文本变化时触发对应的同步计算
    connect(m_frequencyEdit, &HQFreqLineEdit::textChanged, this, syncFromFcSpan);
    connect(m_scanWidthEdit,  &HQFreqLineEdit::textChanged, this, syncFromFcSpan);
    connect(m_startFreqEdit,  &HQFreqLineEdit::textChanged, this, syncFromStartEnd);
    connect(m_endFreqEdit,    &HQFreqLineEdit::textChanged, this, syncFromStartEnd);

    // 给文件输入框绑定
    connect(m_filePathEdit, &HQFileLineEdit::textChanged, this, &CollMonitorMenu::filePathChanged);
}

void CollMonitorMenu::updateCollectMonitorUI()
{
    for (auto label : m_collectMonitorLabels)
    {
        label->setStyleSheet(
        QString("color: %1; background: transparent; border: none;")
            .arg(ThemeManager::instance().colorString("titleBar.collectMonitorColor")));
    }
}

void CollMonitorMenu::setMenuParams(const std::map<std::string, std::string>& params)
{
    int64_t fc = 0;
    int64_t bw = 0;
    int64_t rbw = 0;
    int64_t level = 0;
    std::string filePath;
    m_params = params;
    m_newParams = params;
    for (const auto& p : params)
    {
        if (p.first == "fc")
        {
            m_frequencyEdit->setValue(atoll(p.second.c_str()));
            fc = atoll(p.second.c_str());
            m_innerTestData.m_fc = fc;
        }
        else if (p.first == "span")
        {
            m_scanWidthEdit->setValue(atoll(p.second.c_str()));
            bw = atoll(p.second.c_str());
            m_innerTestData.m_span = bw;
        }
        else if (p.first == "rbw")
        {
            m_rbwEdit->setValue(atoll(p.second.c_str()));
            m_innerTestData.m_rbw = atoll(p.second.c_str());
        }
        else if (p.first == "ref_level")
        {
            m_refLevelSpin->setValue(atoll(p.second.c_str()));
            level = atoll(p.second.c_str());
            m_innerTestData.m_refLevel = level;
        }
        else if (p.first == "name")
        {
            if (p.second == "FILE")
            {
                setTestData(true);
            }
            else
            {
                setTestData(false);
            }
        }
        else if (p.first == "file_path")
        {
            filePath = p.second;
            m_filePathEdit->setText(QString::fromUtf8(filePath.data(), static_cast<int>(filePath.size())));
        }
        else if (p.first == "enable_source_switch" && p.second == "1")
        {
            m_isDbClickSource = true;
        }
    }
    if (m_isTestData)
    {
        // 解析文件信息
        parseFilePath(filePath, fc, bw, rbw, level);
        m_frequencyEdit->setValue(fc);
        m_scanWidthEdit->setValue(bw);
        m_rbwEdit->setValue(rbw);
        m_refLevelSpin->setValue(level);
        emit signalMenuInfo(fc, bw, rbw);
    }

    m_startFreqEdit->setValue(fc - bw / 2);
    m_endFreqEdit->setValue(fc + bw / 2);
    emit signalParams(fc, bw, level);
}

void CollMonitorMenu::onStartBtn_clicked()
{
    if (m_startBtnStatus == false)
    {
        // 校验频率范围是否超界
#if 1
        auto minIt = m_params.find("min_rf");
        auto maxIt = m_params.find("max_rf");
        if (minIt != m_params.end() && maxIt != m_params.end())
        {
            double min_rf_val = std::stod(minIt->second);
            double max_rf_val = std::stod(maxIt->second);

            double fc_val = m_frequencyEdit->value();
            double span_val = m_scanWidthEdit->value();
            double start_freq = fc_val - span_val / 2.0;
            double stop_freq  = fc_val + span_val / 2.0;
            if (start_freq < min_rf_val || stop_freq > max_rf_val)
            {
                ToastWidget::instance()->showError(
                    QStringLiteral("频率范围超界！当前 fc±span/2=[%1, %2] Hz，设备范围=[%3, %4] Hz")
                        .arg(start_freq, 0, 'f', 0).arg(stop_freq, 0, 'f', 0)
                        .arg(min_rf_val, 0, 'f', 0).arg(max_rf_val, 0, 'f', 0));
                return;
            }
        }
#endif
        m_criticalAlertNumLabel->setText("0");
        m_generalAlarmNumLabel->setText("0");
        m_signalTotalNumLabel->setText("0");
        emit signalClearData();// 每次开始就要清空数据
        bool flag = false;
        int64_t fc = 0;
        int64_t bw = 0;

        bool isUpdateTest = false;
        // 判断文件源是否是非FILE
        for (auto& p : m_newParams)
        {
            if (p.first == "name" && p.second != "FILE")
            {
                isUpdateTest = true;
                break;
            }
        }
        // 检查参数是否有变更，如果有变化就设标志
        for (auto& p : m_newParams)
        {
            if (p.first == "fc")
            {
                QString curText = QString::number(m_frequencyEdit->value());
                QString newText = p.second.c_str();
                m_params[p.first] = curText.toStdString();
                fc = curText.toLongLong();
                if (isUpdateTest) m_innerTestData.m_fc = fc;
                if (curText != newText) { flag = true; }
            }
            else if (p.first == "span")
            {
                QString curText = QString::number(m_scanWidthEdit->value());
                QString newText = p.second.c_str();
                m_params[p.first] = curText.toStdString();
                bw = curText.toLongLong();
                if (isUpdateTest) m_innerTestData.m_span = bw;
                if (curText != newText) { flag = true; }
            }
            else if (p.first == "rbw")
            {
                QString curText = QString::number(m_rbwEdit->value());
                QString newText = p.second.c_str();
                m_params[p.first] = curText.toStdString();
                if (isUpdateTest) m_innerTestData.m_rbw = m_rbwEdit->value();
                if (curText != newText) { flag = true; }
            }
            else if (p.first == "ref_level")
            {
                double curVal = m_refLevelSpin->value();
                double newVal = atof(p.second.c_str());
                m_params[p.first] = std::to_string(curVal);
                if (isUpdateTest) m_innerTestData.m_refLevel = curVal;
                if (!qFuzzyCompare(curVal, newVal)) { flag = true; }
            }
            else if (p.first == "file_path")
            {
                QString curText = m_filePathEdit->text();
                QString newText = p.second.c_str();
                m_params[p.first] = curText.toUtf8().data();
                if (curText != newText) { flag = true; }
            }
        }
        if (!(fc - (bw / 2) >= 9 * 1e3 && fc + (bw / 2) <= 6 * 1e9))
        {
            QString errInfo = QStringLiteral("参数错误 起止频率范围：9kHz-6GHz, start:%1 == end:%2").arg(fc - bw / 2).arg(fc + bw / 2);
            LOG_WARN("%s", errInfo.toStdString().c_str());
            ToastWidget::instance()->showError(errInfo);
            return;
        }
        if (m_isTestData)
        {
            const QString filePath = m_filePathEdit->text();
            if (!QFile::exists(filePath))
            {
                QString errInfo = QStringLiteral("回放文件不存在: %1").arg(filePath);
                LOG_WARN("%s", errInfo.toStdString().c_str());
                ToastWidget::instance()->showError(errInfo);
                return;
            }
        }
        if (flag)
        {
            haiq::GuiRequestData req;
            req._cmdType = haiq::CMDType::CMD_RESET;
            emit signalRequest(req);

            req._cmdType = haiq::CMDType::CMD_PARAM;
            req._paramTable["SpectrumSource"] = m_params;
            emit signalRequest(req);

            int64_t fc_val = m_frequencyEdit->value();
            int64_t span_val = m_scanWidthEdit->value();
            emit signalParams(fc_val, span_val, m_refLevelSpin->value());
        }
    }

    haiq::GuiRequestData req;
    req._cmdType = m_startBtnStatus ? haiq::CMDType::CMD_STOP : haiq::CMDType::CMD_RESUME;
    req._uuid = 1;// 标识点击的开始按钮
    emit signalRequest(req);
}

void CollMonitorMenu::onPauseBtn_clicked()
{
    // haiq::GuiRequestData req; 当前实时情况不存在暂停
    // req._cmdType = m_pauseBtnStatus ? haiq::CMDType::CMD_RESUME : haiq::CMDType::CMD_PAUSE;
    // req._uuid = 2;// 标识点击的暂停按钮
    // emit signalRequest(req);
    m_pauseBtnStatus = !m_pauseBtnStatus;
    if (m_pauseBtnStatus)
    {
        m_pauseBtn->setText(QStringLiteral("继续查看"));
        m_pauseBtn->setButtonStyle(HQButton::Primary_normal);
        m_pauseBtn->setIconType(HQButton::Start);
        emit signalIsUpdateData(false);
        emit signalListDbRecord(true);
    }
    else
    {
        m_pauseBtn->setText(QStringLiteral("暂停查看"));
        m_pauseBtn->setButtonStyle(HQButton::Error);
        m_pauseBtn->setIconType(HQButton::Pause);
        emit signalIsUpdateData(true);
        emit signalListDbRecord(false);
    }
}

bool CollMonitorMenu::eventFilter(QObject *watched, QEvent *event)
{
    // 开始按钮右键双击：打开文件源切换弹窗
    if (watched == m_startBtn && event->type() == QEvent::MouseButtonDblClick)
    {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::RightButton)
        {
            onStartBtnDoubleClicked();
            return true;   // 消费该双击事件，避免按钮默认处理
        }
    }
    return QWidget::eventFilter(watched, event);
}

void CollMonitorMenu::onStartBtnDoubleClicked()
{
    if (!m_isDbClickSource || m_startBtnStatus) return;

    std::string filePath;
    bool isFile = false;
    HQSourceSwitchDialog dlg(this);
    for (auto& p : m_newParams)
    {
        if (p.first == "name")
        {
            dlg.setCurrentSource(p.second.c_str());
        }
        else if (p.first == "file_path")
        {
            filePath = p.second;
        }
    }
    if (dlg.exec() == QDialog::Accepted)
    {
        const QString source = dlg.selectedSource();
        if (!source.isEmpty())
        {
            for (auto& p : m_newParams)
            {
                if (p.first == "name")
                {
                    p.second = source.toUtf8().data();
                    auto it = m_params.find(p.first);
                    if (it != m_params.end())
                    {
                        it->second = p.second;
                    }
                    if (p.second == "FILE")
                    {
                        isFile = true;
                        setTestData(true);
                        // 解析文件信息
                        int64_t fc, bw, rbw, level;
                        parseFilePath(filePath, fc, bw, rbw, level);
                        m_frequencyEdit->setValue(fc);
                        m_scanWidthEdit->setValue(bw);
                        m_rbwEdit->setValue(rbw);
                        m_refLevelSpin->setValue(level);
                        emit signalMenuInfo(fc, bw, rbw);
                    }
                    else
                    {
                        setTestData(false);
                    }
                    // 通知外部：文件源已切换（更新状态栏设备名称及频率标签显隐）
                    emit signalSourceSwitch(source);
                    emit signalClearData();
                    break;
                }
            }

            if(!isFile)
            {
                m_frequencyEdit->setValue(m_innerTestData.m_fc);
                m_scanWidthEdit->setValue(m_innerTestData.m_span);
                m_rbwEdit->setValue(m_innerTestData.m_rbw);
                m_refLevelSpin->setValue(m_innerTestData.m_refLevel);
            }
            haiq::GuiRequestData req;
            req._cmdType = haiq::CMDType::CMD_RESET;
            emit signalRequest(req);

            req._cmdType = haiq::CMDType::CMD_PARAM;
            req._paramTable["SpectrumSource"] = m_params;
            emit signalRequest(req);
        }
    }
}

void CollMonitorMenu::setContentEnable(bool enable)
{
    if (m_isTestData) return;
    m_frequencyEdit->setEnabled(enable);
    m_scanWidthEdit->setEnabled(enable);
    m_rbwEdit->setEnabled(enable);
    m_refLevelSpin->setEnabled(enable);
    m_refLevelSpinUnit->setEnabled(enable);
    m_startFreqEdit->setEnabled(enable);
    m_endFreqEdit->setEnabled(enable);
    m_filePathEdit->setEnabled(enable);
}

void CollMonitorMenu::parseFilePath(const std::string& filePath, int64_t& fc, int64_t& bw, int64_t& rbw, int64_t& refLevel)
{
    // 解析文件，文件格式：20260721_143743_967_Fc=3025000000_Bw=5950000000_Rbw=40000_Reflevel=-35_SpectrumLen=304642.dat
    // 从文件名中提取 Fc/Bw/Rbw/Reflevel 等关键参数值

    // 提取文件名（去除路径部分）
    std::string fileName = filePath;
    size_t pos = fileName.find_last_of("/\\");
    if (pos != std::string::npos) {
        fileName = fileName.substr(pos + 1);
    }

    // 根据 key 前缀查找等号后的数值，数值结束于下一个 '_' 或 '.' 或字符串末尾
    auto parseValue = [](const std::string& str, const std::string& key) -> int64_t {
        size_t keyPos = str.find(key);
        if (keyPos == std::string::npos)
            return 0;

        size_t valPos  = keyPos + key.length();                     // 等号后的数值起始位置
        size_t endPos  = str.find_first_of("_.", valPos);           // 数值结束分隔符
        if (endPos == std::string::npos)
            endPos = str.length();

        std::string valStr = str.substr(valPos, endPos - valPos);
        return atoll(valStr.c_str());
    };

    fc       = parseValue(fileName, "Fc=");
    bw       = parseValue(fileName, "Bw=");
    rbw      = parseValue(fileName, "Rbw=");
    refLevel = parseValue(fileName, "Reflevel=");
}

void CollMonitorMenu::setTestData(bool flag)
{
    m_isTestData = flag;
    // 隐藏频率相关控件
    m_frequencyEdit->setVisible(!flag);
    m_scanWidthEdit->setVisible(!flag);
    m_rbwEdit->setVisible(!flag);
    m_startFreqEdit->setEnabled(!flag);
    m_endFreqEdit->setEnabled(!flag);
    m_refLevelSpin->setEnabled(!flag);
    m_refLevelSpinUnit->setEnabled(!flag);
    m_frequencyLabel->setVisible(!flag);
    m_scanWidthLabel->setVisible(!flag);
    m_rbwLabel->setVisible(!flag);
    // 显示文件路径控件
    m_filePathLabel->setVisible(flag);
    m_filePathEdit->setVisible(flag);
}
