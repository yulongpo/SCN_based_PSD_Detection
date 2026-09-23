#include "StatusBarWidget.h"
#include "FrequencySpinBox.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QProgressBar>
#include <QString>
#include <QVBoxLayout>

namespace scn::app
{

namespace
{
class LoadIndicator final : public QWidget
{
public:
    LoadIndicator(const QString& iconPath, const QString& name,
                  const QString& color, QWidget* parent)
        : QWidget(parent), m_color(color)
    {
        setFixedWidth(176);
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(5);

        auto* icon = new QLabel(this);
        icon->setFixedSize(16, 16);
        icon->setPixmap(QPixmap(iconPath).scaled(16, 16, Qt::KeepAspectRatio,
                                                  Qt::SmoothTransformation));
        layout->addWidget(icon);

        auto* title = new QLabel(name, this);
        title->setObjectName(QStringLiteral("loadTitle"));
        layout->addWidget(title);

        m_bar = new QProgressBar(this);
        m_bar->setRange(0, 100);
        m_bar->setValue(0);
        m_bar->setTextVisible(false);
        m_bar->setFixedHeight(4);
        m_bar->setFixedWidth(112);
        m_bar->setStyleSheet(QStringLiteral(
            "QProgressBar { background:#606D79; border:0; border-radius:2px; }"
            "QProgressBar::chunk { background:%1; border-radius:2px; }").arg(color));
        layout->addWidget(m_bar);

        m_value = new QLabel(QStringLiteral("0%"), this);
        m_value->setObjectName(QStringLiteral("loadValue"));
        m_value->setStyleSheet(QStringLiteral("color:%1;").arg(color));
        m_value->setFixedWidth(30);
        m_value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        layout->addWidget(m_value);
    }

    void setValue(int value)
    {
        value = qBound(0, value, 100);
        m_bar->setValue(value);
        m_value->setText(QStringLiteral("%1%").arg(value));
    }

private:
    QString m_color;
    QProgressBar* m_bar = nullptr;
    QLabel* m_value = nullptr;
};

QLabel* statusLabel(const QString& text, QWidget* parent, const QString& objectName,
                    const QString& color)
{
    auto* result = new QLabel(text, parent);
    result->setObjectName(objectName);
    result->setStyleSheet(QStringLiteral("color:%1; background:transparent; border:0;").arg(color));
    result->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    return result;
}

QString frequencyText(double hz)
{
    return FrequencySpinBox::formatFrequency(hz);
}
}

StatusBarWidget::StatusBarWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("statusBarWidget"));
    setFixedHeight(32);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 0, 12, 0);
    layout->setSpacing(8);

    m_cpuLoad = new LoadIndicator(QStringLiteral(":/status/cpu.png"), QStringLiteral("CPU"),
                                  QStringLiteral("#1B97BE"), this);
    m_gpuLoad = new LoadIndicator(QStringLiteral(":/status/gpu.png"), QStringLiteral("GPU"),
                                  QStringLiteral("#A2A22C"), this);
    m_ramLoad = new LoadIndicator(QStringLiteral(":/status/ram.png"), QStringLiteral("RAM"),
                                  QStringLiteral("#2CA25B"), this);
    layout->addWidget(m_cpuLoad);
    auto addSeparator = [this, layout] {
        auto* separator = new QWidget(this);
        separator->setFixedSize(1, 16);
        separator->setStyleSheet(QStringLiteral("background:#3a3a46;"));
        layout->addWidget(separator);
    };
    addSeparator();
    layout->addWidget(m_gpuLoad);
    addSeparator();
    layout->addWidget(m_ramLoad);

    m_fcLabel = statusLabel(QString(), this, QStringLiteral("statusLabel"), QStringLiteral("#606D79"));
    m_bwLabel = statusLabel(QString(), this, QStringLiteral("statusLabel"), QStringLiteral("#606D79"));
    m_rbwLabel = statusLabel(QString(), this, QStringLiteral("statusLabel"), QStringLiteral("#606D79"));
    m_fcLabel->setVisible(false);
    m_bwLabel->setVisible(false);
    m_rbwLabel->setVisible(false);
    layout->addStretch(1);
    layout->addWidget(m_fcLabel);
    layout->addWidget(m_bwLabel);
    layout->addWidget(m_rbwLabel);

    layout->addWidget(statusLabel(QStringLiteral("时间:"), this, QStringLiteral("statusLabel"), QStringLiteral("#606D79")));
    m_timeValue = statusLabel(QStringLiteral("--:--:--"), this, QStringLiteral("timeValue"), QStringLiteral("#FFFFFF"));
    layout->addWidget(m_timeValue);
    layout->addWidget(statusLabel(QStringLiteral("采集设备:"), this, QStringLiteral("statusLabel"), QStringLiteral("#606D79")));
    m_deviceValue = statusLabel(QStringLiteral("BB60C"), this, QStringLiteral("statusValue"), QStringLiteral("#2CA25B"));
    layout->addWidget(m_deviceValue);
    layout->addWidget(statusLabel(QStringLiteral("BB60C接入:"), this, QStringLiteral("statusLabel"), QStringLiteral("#606D79")));
    m_bb60cStatus = statusLabel(QStringLiteral("未连接"), this, QStringLiteral("statusValue"), QStringLiteral("#9AA6B2"));
    layout->addWidget(m_bb60cStatus);
    layout->addWidget(statusLabel(QStringLiteral("海得罗捷接入:"), this, QStringLiteral("statusLabel"), QStringLiteral("#606D79")));
    m_harogicStatus = statusLabel(QStringLiteral("未接入"), this, QStringLiteral("statusValue"), QStringLiteral("#9AA6B2"));
    layout->addWidget(m_harogicStatus);
    layout->addWidget(statusLabel(QStringLiteral("设备状态:"), this, QStringLiteral("statusLabel"), QStringLiteral("#606D79")));
    m_deviceStatus = statusLabel(QStringLiteral("未连接"), this, QStringLiteral("statusValue"), QStringLiteral("#9AA6B2"));
    layout->addWidget(m_deviceStatus);
    layout->addWidget(statusLabel(QStringLiteral("网络接入:"), this, QStringLiteral("statusLabel"), QStringLiteral("#606D79")));
    layout->addWidget(statusLabel(QStringLiteral("正常"), this, QStringLiteral("statusValue"), QStringLiteral("#2CA25B")));
    layout->addWidget(statusLabel(QStringLiteral("自检结果:"), this, QStringLiteral("statusLabel"), QStringLiteral("#606D79")));
    layout->addWidget(statusLabel(QStringLiteral("正常"), this, QStringLiteral("statusValue"), QStringLiteral("#2CA25B")));
}

void StatusBarWidget::setMenuInfo(double centerFrequencyHz, double bandwidthHz,
                                  double resolutionBandwidthHz)
{
    m_fcLabel->setText(QStringLiteral("中心频率:") + frequencyText(centerFrequencyHz));
    m_bwLabel->setText(QStringLiteral("扫宽:") + frequencyText(bandwidthHz));
    m_rbwLabel->setText(QStringLiteral("带宽分辨率:") + frequencyText(resolutionBandwidthHz));
    setMenuInfoVisible(true);
}

void StatusBarWidget::setMenuInfoVisible(bool visible)
{
    m_fcLabel->setVisible(visible);
    m_bwLabel->setVisible(visible);
    m_rbwLabel->setVisible(visible);
}

void StatusBarWidget::setDeviceValue(const QString& value)
{
    m_deviceValue->setText(value);
    if (value.compare(QStringLiteral("BB60C"), Qt::CaseInsensitive) == 0) {
        setDeviceConnectionStatus(QStringLiteral("BB60C"), m_bb60cStatus->text(),
                                  m_bb60cStatus->text() == QStringLiteral("已连接"));
    } else if (value.contains(QStringLiteral("海得罗捷")) ||
               value.compare(QStringLiteral("Harogic"), Qt::CaseInsensitive) == 0) {
        setDeviceConnectionStatus(QStringLiteral("海得罗捷"), m_harogicStatus->text(),
                                  m_harogicStatus->text() == QStringLiteral("已连接"));
    } else if (m_deviceStatus) {
        m_deviceStatus->setText(QStringLiteral("未连接"));
        m_deviceStatus->setStyleSheet(QStringLiteral("color:#9AA6B2; background:transparent; border:0;"));
    }
}

void StatusBarWidget::setDeviceConnectionStatus(const QString& device,
                                                const QString& status,
                                                bool connected)
{
    QLabel* target = nullptr;
    if (device.compare(QStringLiteral("BB60C"), Qt::CaseInsensitive) == 0) {
        target = m_bb60cStatus;
    } else if (device.contains(QStringLiteral("海得罗捷")) ||
               device.compare(QStringLiteral("Harogic"), Qt::CaseInsensitive) == 0) {
        target = m_harogicStatus;
    }
    if (!target) return;

    const QString color = connected ? QStringLiteral("#2CA25B") : QStringLiteral("#9AA6B2");
    target->setText(status);
    target->setStyleSheet(QStringLiteral("color:%1; background:transparent; border:0;").arg(color));
    if (m_deviceValue && m_deviceStatus &&
        ((device.compare(QStringLiteral("BB60C"), Qt::CaseInsensitive) == 0 &&
          m_deviceValue->text().compare(QStringLiteral("BB60C"), Qt::CaseInsensitive) == 0) ||
         (device.contains(QStringLiteral("海得罗捷")) &&
          m_deviceValue->text().compare(QStringLiteral("Harogic"), Qt::CaseInsensitive) == 0))) {
        m_deviceStatus->setText(status);
        m_deviceStatus->setStyleSheet(QStringLiteral("color:%1; background:transparent; border:0;").arg(color));
    }
}

void StatusBarWidget::setTime(const QString& value)
{
    m_timeValue->setText(value);
}

void StatusBarWidget::setLoadValues(int cpu, int gpu, int ram)
{
    static_cast<LoadIndicator*>(m_cpuLoad)->setValue(cpu);
    static_cast<LoadIndicator*>(m_gpuLoad)->setValue(gpu);
    static_cast<LoadIndicator*>(m_ramLoad)->setValue(ram);
}

} // namespace scn::app
