#pragma once

#include <QWidget>

class QLabel;

namespace scn::app
{

class StatusBarWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit StatusBarWidget(QWidget* parent = nullptr);

    void setMenuInfo(double centerFrequencyHz, double bandwidthHz, double resolutionBandwidthHz);
    void setMenuInfoVisible(bool visible);
    void setDeviceValue(const QString& value);
    void setLoadValues(int cpu, int gpu, int ram);
    void setTime(const QString& value);

private:
    QLabel* m_fcLabel = nullptr;
    QLabel* m_bwLabel = nullptr;
    QLabel* m_rbwLabel = nullptr;
    QLabel* m_timeValue = nullptr;
    QLabel* m_deviceValue = nullptr;
    QWidget* m_cpuLoad = nullptr;
    QWidget* m_gpuLoad = nullptr;
    QWidget* m_ramLoad = nullptr;
};

} // namespace scn::app
