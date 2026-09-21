#include "StatusBarWidget.h"
#include "comm/ScreenScale.h"
#include "comm/CommonMacros.h"
#include "comm/ThemeManager.h"
#include "widgets/ProgressBarWidget.h"

#include <QColor>
#include <QHBoxLayout>
#include <QPixmap>
#include <QStyle>
#include <QToolTip>

#include "comm/FontManager.h"
#include "monitor/SystemMonitor.h"
#include "radioai/icd/HQSigMF.hpp"

// ============================================================
// 构造
// ============================================================

StatusBarWidget::StatusBarWidget(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("statusBar"));
    setAttribute(Qt::WA_StyledBackground, true);

    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

    // 固定高度
    setFixedHeight(DPR_INT(32 * scale, dpr, 0));

    // ---- 创建标签 ----
    m_detectionLabel            = createLabel(QStringLiteral("statusLabel"));
    m_detectionValue            = createLabel(QStringLiteral("statusValue"));
    m_alertLabel                = createLabel(QStringLiteral("statusLabel"));
    m_timeLabel                 = createLabel(QStringLiteral("timeLabel"));
    m_timeValue                 = createLabel(QStringLiteral("timeValue"));
    m_deviceLabel               = createLabel(QStringLiteral("statusLabel"));
    m_deviceValue               = createLabel(QStringLiteral("statusValue"));
    m_deviceCollectStatusLabel  = createLabel(QStringLiteral("deviceCollectStatusLabel"));
    m_deviceCollectStatusValue  = createLabel(QStringLiteral("deviceCollectStatusValue"));
    m_networkCollectInfoLabel   = createLabel(QStringLiteral("networkCollectInfoLabel"));
    m_networkCollectInfoValue   = createLabel(QStringLiteral("networkCollectInfoValue"));
    m_selfCheckStatusLabel      = createLabel(QStringLiteral("selfCheckStatusLabel"));
    m_selfCheckStatusValue      = createLabel(QStringLiteral("selfCheckStatusValue"));

    m_fcLabel                   = createLabel(QStringLiteral("statusLabel"));
    m_bwLabel                   = createLabel(QStringLiteral("statusLabel"));
    m_rbwLabel                  = createLabel(QStringLiteral("statusLabel"));
    m_fcLabel->setVisible(false);
    m_bwLabel->setVisible(false);
    m_rbwLabel->setVisible(false);

    // 设置文本
    m_detectionLabel->setText(QStringLiteral("实时监测:"));
    m_detectionLabel->setVisible(false);
    m_detectionValue->setText(QString());
    m_detectionValue->setVisible(false);
    m_alertLabel->setText(QStringLiteral("告警已启用"));m_alertLabel->setVisible(false);
    m_timeLabel->setText(QStringLiteral("时间:"));
    m_timeValue->setText(QStringLiteral(""));
    m_deviceLabel->setText(QStringLiteral("采集设备:"));
    m_deviceValue->setText(QStringLiteral("BB60C"));
    m_deviceCollectStatusLabel->setText(QStringLiteral("设备状态:"));m_deviceCollectStatusLabel->setVisible(false);
    m_deviceCollectStatusValue->setText(QStringLiteral("已连接"));m_deviceCollectStatusValue->setVisible(false);
    m_networkCollectInfoLabel->setText(QStringLiteral("网络接入:"));m_networkCollectInfoLabel->setVisible(false);
    m_networkCollectInfoValue->setText(QStringLiteral("GbE"));m_networkCollectInfoValue->setVisible(false);
    m_selfCheckStatusLabel->setText(QStringLiteral("自检结果:"));m_selfCheckStatusLabel->setVisible(false);
    m_selfCheckStatusValue->setText(QStringLiteral("正常"));m_selfCheckStatusValue->setVisible(false);

    // ---- 创建分隔条 ----
    m_sep1 = createSeparator();
    m_sep2 = createSeparator();
    m_sep3 = createSeparator();
    m_sep4 = createSeparator();
    m_sep3->setVisible(false);
    m_sep4->setVisible(false);

    // ---- CPU / GPU / RAM 进度条 ----
    m_cpuProgress = createProgressBar(QStringLiteral(":/status/cpu.png"),
                                       QStringLiteral("CPU"),
                                       QStringLiteral("statusBar.cpuProgressFill"));
    m_gpuProgress = createProgressBar(QStringLiteral(":/status/gpu.png"),
                                       QStringLiteral("GPU"),
                                       QStringLiteral("statusBar.gpuProgressFill"));
    m_ramProgress = createProgressBar(QStringLiteral(":/status/ram.png"),
                                       QStringLiteral("RAM"),
                                       QStringLiteral("statusBar.ramProgressFill"));

    // 演示初始值（后续可替换为真实数据）
    m_cpuProgress->setValue(0);
    m_gpuProgress->setValue(0);
    m_ramProgress->setValue(0);

    // ---- 布局 ----
    const int pad          = DPR_INT(12 * scale, dpr, 0);
    const int itemSpacing  = DPR_INT(8 * scale, dpr, 0);
    const int labelSpacing = DPR_INT(4 * scale, dpr, 0);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(pad, 0, pad, 0);
    layout->setSpacing(0);

    layout->addWidget(m_cpuProgress);
    layout->addSpacing(itemSpacing);
    layout->addWidget(m_sep1);
    layout->addSpacing(itemSpacing);
    layout->addWidget(m_gpuProgress);
    layout->addSpacing(itemSpacing);
    layout->addWidget(m_sep2);
    layout->addSpacing(itemSpacing);
    layout->addWidget(m_ramProgress);
    layout->addSpacing(itemSpacing);
    layout->addWidget(m_sep3);
    layout->addSpacing(itemSpacing);
    layout->addWidget(m_detectionLabel);
    layout->addWidget(m_detectionValue);
    layout->addSpacing(itemSpacing);
    layout->addWidget(m_sep4);
    layout->addSpacing(itemSpacing);
    layout->addWidget(m_alertLabel);

    layout->addWidget(m_fcLabel);
    layout->addWidget(m_bwLabel);
    layout->addWidget(m_rbwLabel);

    layout->addStretch(1);   // 将全部内容推到左侧
    layout->addWidget(m_timeLabel);
    layout->addSpacing(itemSpacing);
    layout->addWidget(m_timeValue);
    layout->addSpacing(itemSpacing);
    layout->addWidget(m_deviceLabel);
    layout->addSpacing(itemSpacing);
    layout->addWidget(m_deviceValue);
    layout->addSpacing(itemSpacing);
    layout->addWidget(m_deviceCollectStatusLabel);
    layout->addSpacing(itemSpacing);
    layout->addWidget(m_deviceCollectStatusValue);
    layout->addSpacing(itemSpacing);
    layout->addWidget(m_networkCollectInfoLabel);
    layout->addSpacing(itemSpacing);
    layout->addWidget(m_networkCollectInfoValue);
    layout->addSpacing(itemSpacing);
    layout->addWidget(m_selfCheckStatusLabel);
    layout->addSpacing(itemSpacing);
    layout->addWidget(m_selfCheckStatusValue);
    auto updateTime = [this]() {
        m_timeValue->setText(QDateTime::currentDateTime().toString("HH:mm:ss"));
    };
    updateTime();
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, updateTime);
    timer->start(1000);

    // ---- 初始样式 ----
    applyStyle();

    // ---- 监听主题切换 ----
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, [this](bool /*night*/) {
                applyStyle();
            });

    // ---- 连接系统监控信号 ----
    auto &monitor = SystemMonitor::instance();
    connect(&monitor, &SystemMonitor::cpuUsageUpdated,
            this, [this](int pct) { m_cpuProgress->setValue(pct); });
    connect(&monitor, &SystemMonitor::gpuUsageUpdated,
            this, [this](int pct) { m_gpuProgress->setValue(pct); });
    connect(&monitor, &SystemMonitor::ramUsageUpdated,
            this, [this](int pct) { m_ramProgress->setValue(pct); });
    // ---- 内存监控信号（用于 hover 时显示详情提示框） ----
    connect(&monitor, &SystemMonitor::ramMemoryUpdated,
            this, [this](qint64 totalKB, qint64 usedKB) {
        m_cpuMemoryTotalKB = totalKB;
        m_cpuMemoryUsedKB  = usedKB;
        updateMemoryTooltip(m_ramProgress, totalKB, usedKB);
    });

    connect(&monitor, &SystemMonitor::gpuMemoryUpdated,
            this, [this](qint64 totalKB, qint64 usedKB) {
        m_gpuMemoryTotalKB = totalKB;
        m_gpuMemoryUsedKB  = usedKB;
        updateMemoryTooltip(m_gpuProgress, totalKB, usedKB);
    });
}

void StatusBarWidget::setMenuInfo(int64_t fc, int64_t bw, int64_t rbw)
{
    auto calcUnit = [this](int64_t val)->QString
    {
        if (val > 1000 * 1000 * 1000)
        {
            return QString::number(val / 1e9, 'f', 9) + " GHz";
        }
        else if (val > 1000 * 1000)
        {
            return QString::number(val / 1e6, 'f', 6) + " MHz";
        }
        else
        {
            return QString::number(val / 1e3, 'f', 3) + " kHz";
        }
    };
    m_fcLabel->setText(QStringLiteral("  中心频率:") + calcUnit(fc));
    m_bwLabel->setText(QStringLiteral("  扫宽:") + calcUnit(bw));
    m_rbwLabel->setText(QStringLiteral("  带宽分辨率:") + calcUnit(rbw));
    m_fcLabel->setVisible(true);
    m_bwLabel->setVisible(true);
    m_rbwLabel->setVisible(true);
}

void StatusBarWidget::setDeviceValue(QString value)
{
    m_deviceValue->setText(value);
}

void StatusBarWidget::setMenuInfoVisible(bool visible)
{
    m_fcLabel->setVisible(visible);
    m_bwLabel->setVisible(visible);
    m_rbwLabel->setVisible(visible);
}

void StatusBarWidget::setDetectionProgress(int stage, int accumulatedFrames,
                                           int minOutputFrames, int requiredFrames)
{
    const int accumulated = qMax(0, accumulatedFrames);
    const int minOutput = qMax(0, minOutputFrames);
    const int required = qMax(0, requiredFrames);

    QString text;
    QColor color;
    switch (static_cast<DetectionStage>(stage))
    {
    case DetectionStage::WARMING_UP:
        text = QStringLiteral("检测预热 %1/%2").arg(accumulated).arg(required);
        color = QColor(137, 138, 139);
        break;
    case DetectionStage::PROVISIONAL:
        text = QStringLiteral("快速检测 %1/%2，结果暂定").arg(accumulated).arg(required);
        color = QColor(10, 140, 254);
        break;
    case DetectionStage::READY:
        text = QStringLiteral("检测已就绪");
        color = QColor(46, 160, 67);
        break;
    case DetectionStage::ERROR_STATE:
    default:
        text = QStringLiteral("检测错误");
        color = QColor(230, 62, 62);
        break;
    }

    const QString tooltip =
        QStringLiteral("已累积 %1 帧；最少输出 %2 帧；稳态窗口 %3 帧")
            .arg(accumulated).arg(minOutput).arg(required);
    m_detectionLabel->setToolTip(tooltip);
    m_detectionValue->setText(text);
    m_detectionValue->setToolTip(tooltip);
    m_detectionValue->setStyleSheet(QStringLiteral(
        "color: rgb(%1,%2,%3); background: transparent; border: none;")
        .arg(color.red()).arg(color.green()).arg(color.blue()));

    m_sep3->setVisible(true);
    m_sep4->setVisible(true);
    m_detectionLabel->setVisible(true);
    m_detectionValue->setVisible(true);
}

void StatusBarWidget::clearDetectionProgress()
{
    m_detectionValue->clear();
    m_detectionLabel->setToolTip(QString());
    m_detectionValue->setToolTip(QString());
    m_detectionValue->setStyleSheet(QString());
    m_detectionLabel->setVisible(false);
    m_detectionValue->setVisible(false);
    m_sep3->setVisible(false);
    m_sep4->setVisible(false);
}

// ============================================================
// 创建子控件辅助函数
// ============================================================

QLabel *StatusBarWidget::createLabel(const QString &objectName) const
{
    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

    auto *label = new QLabel(const_cast<StatusBarWidget *>(this));
    label->setObjectName(objectName);

    // 字体：SourceHanSansSC-Normal，weight=400（Normal），大小 12px（DPR 感知）
    QFont f = FontManager::instance().font(DPR_INT(12 * scale, dpr, 0), QFont::Normal);
    label->setFont(f);

    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    return label;
}

QWidget *StatusBarWidget::createSeparator() const
{
    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

    auto *sep = new QWidget(const_cast<StatusBarWidget *>(this));
    sep->setObjectName(QStringLiteral("statusSeparator"));
    sep->setFixedWidth(DPR_INT(1 * scale, dpr, 1));
    sep->setFixedHeight(DPR_INT(16 * scale, dpr, 0));   // 分隔条高度

    return sep;
}

// ============================================================
// 创建统一进度条
// ============================================================

ProgressBarWidget *StatusBarWidget::createProgressBar(
    const QString &iconPath, const QString &label,
    const QString &fillColorKey) const
{
    const auto &ss     = ScreenScale::instance();
    const qreal dpr    = ss.dpr();
    const double scale = ss.scale();

    auto *bar = new ProgressBarWidget(const_cast<StatusBarWidget *>(this));

    // 图标 16x16
    QPixmap pixmap(iconPath);
    if (!pixmap.isNull())
        bar->setIcon(pixmap, 16);
    bar->setLabel(label);

    // 字体 12px（与状态栏其他文字一致）
    QFont f(QStringLiteral("SourceHanSansSC-Normal"), -1, QFont::Normal);
    f.setPixelSize(DPR_INT(12 * scale, dpr, 0));
    bar->setBarFont(f);

    // 进度条高度 4px、固定宽度 160px
    bar->setBarHeight(DPR_INT(4 * scale, dpr, 1));
    bar->setFixedWidth(DPR_INT(160 * scale, dpr, 0));

    // 背景色统一用 cpuProgressBg
    const auto &tm = ThemeManager::instance();
    bar->setBarBgColor(tm.color(QStringLiteral("statusBar.cpuProgressBg")));

    return bar;
}

// ============================================================
// 样式应用
// ============================================================

void StatusBarWidget::applyStyle()
{
    const auto &ss       = ScreenScale::instance();
    const qreal dpr      = ss.dpr();
    const double scale   = ss.scale();
    const auto &tm = ThemeManager::instance();

    const QString labelColor  = tm.colorString(QStringLiteral("statusBar.labelColor"));
    const QString valueColor  = tm.colorString(QStringLiteral("statusBar.valueColor"));
    const QString sepColor    = tm.colorString(QStringLiteral("statusBar.separatorColor"));
    const QString topBdrColor = tm.colorString(QStringLiteral("statusBar.topBorderColor"));

    // 用 QSS 为所有子控件统一样式
    // 背景透明，让内容面板的背景色透出；顶部 1px 边框与上下内容区分
    setStyleSheet(QStringLiteral(
        "StatusBarWidget {"
        "  background: transparent;"
        "  border-top: %4px solid %5;"
        "}"
        "QLabel#statusLabel,QLabel#deviceCollectStatusLabel,QLabel#networkCollectInfoLabel,QLabel#selfCheckStatusLabel {"
        "  color: %1;"
        "  background: transparent;"
        "  border: none;"
        "}"
        "QLabel#statusValue,QLabel#deviceCollectStatusValue,QLabel#networkCollectInfoValue,QLabel#selfCheckStatusValue {"
        "  color: %2;"
        "  background: transparent;"
        "  border: none;"
        "}"
        "QLabel#timeLabel {"
        "  color: %1;"
        "  background: transparent;"
        "  border: none;"
        "}"
        "QLabel#timeValue {"
        "  color: white;"
        "  background: transparent;"
        "  border: none;"
        "}"
        "QWidget#statusSeparator {"
        "  background-color: %3;"
        "}"
        "QToolTip {"
        "  background-color: rgb(45, 45, 48);"
        "  color: rgb(220, 220, 220);"
        "  border: 1px solid rgb(20, 20, 20);"
        "  padding: 4px;"
        "  font-size: 9pt;"
        "}"
    ).arg(labelColor,
          valueColor,
          sepColor,
          QString::number(DPR_INT(1 * scale, dpr, 1)),
          topBdrColor));

    // ---- 进度条颜色 ----
    // 百分比文字使用填充色（"填充色同步给右侧进度文字"）
    auto applyProgressColors = [&](ProgressBarWidget *bar, const QString &fillKey) {
        if (!bar) return;
        bar->setBarBgColor(tm.color(QStringLiteral("statusBar.cpuProgressBg")));
        const QColor fillColor = tm.color(fillKey);
        bar->setBarFillColor(fillColor);
        bar->setTextColor(fillColor);
    };
    applyProgressColors(m_cpuProgress, QStringLiteral("statusBar.cpuProgressFill"));
    applyProgressColors(m_gpuProgress, QStringLiteral("statusBar.gpuProgressFill"));
    applyProgressColors(m_ramProgress, QStringLiteral("statusBar.ramProgressFill"));

    // 强制 QSS 刷新
    this->style()->unpolish(this);
    this->style()->polish(this);
}

// ============================================================
// 内存提示框辅助
// ============================================================

void StatusBarWidget::updateMemoryTooltip(QWidget *widget, qint64 totalKB, qint64 usedKB)
{
    if (!widget)
        return;

    // 格式化为合适的单位
    auto formatMem = [](qint64 kb) -> QString {
        if (kb >= 1024 * 1024) {
            // >= 1 GB 显示 GB
            return QStringLiteral("%1 GB").arg(kb / (1024.0 * 1024.0), 0, 'f', 1);
        } else if (kb >= 1024) {
            // >= 1 MB 显示 MB
            return QStringLiteral("%1 MB").arg(kb / 1024.0, 0, 'f', 1);
        } else {
            // < 1 MB 显示 KB
            return QStringLiteral("%1 KB").arg(kb);
        }
    };

    const QString tip = QStringLiteral("已使用 %1 / 总量 %2")
                            .arg(formatMem(usedKB), formatMem(totalKB));
    widget->setToolTip(tip);
}

// ============================================================
// 绘制事件 — 确保背景填充正确（配合 QSS）
// ============================================================

void StatusBarWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    // QSS 已通过 background-color 处理背景，此处不需要额外绘制。
    // 但保留 paintEvent 以便将来需要自定义绘制（如分隔线动画）时扩展。
    QWidget::paintEvent(event);
}
