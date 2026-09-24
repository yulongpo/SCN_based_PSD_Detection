#pragma once

#include "../../../algorithm/DetectionConfig.h"

#include <QWidget>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QTableWidget;

namespace scn::app
{

class FrequencySpinBox;

class DetectionConfigWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit DetectionConfigWidget(QWidget* parent = nullptr);

    algorithm::DetectionConfig config() const;
    void setConfig(const algorithm::DetectionConfig& config);
    void setFeedback(const QString& message);

signals:
    void applyRequested();

private slots:
    void addChannelPrior();
    void removeChannelPrior();

private:
    void buildUi();

    QCheckBox* m_detectionEnabled = nullptr;
    QLineEdit* m_modelPath = nullptr;
    QSpinBox* m_gpuIndex = nullptr;
    QSpinBox* m_accumulatorFrames = nullptr;
    QDoubleSpinBox* m_confidence = nullptr;
    QDoubleSpinBox* m_nmsIou = nullptr;
    QSpinBox* m_topK = nullptr;
    QSpinBox* m_maxCandidates = nullptr;
    QSpinBox* m_windowStep = nullptr;
    QLabel* m_windowOverlap = nullptr;
    QDoubleSpinBox* m_cnr = nullptr;
    QDoubleSpinBox* m_fusionIou = nullptr;
    QDoubleSpinBox* m_fusionOverlap = nullptr;
    FrequencySpinBox* m_fusionGap = nullptr;
    QDoubleSpinBox* m_trackOverlap = nullptr;
    QDoubleSpinBox* m_maxMiss = nullptr;
    QCheckBox* m_boundaryStability = nullptr;
    QDoubleSpinBox* m_trackMaxBandwidthRatio = nullptr;
    QDoubleSpinBox* m_trackCenterDistanceRatio = nullptr;
    QSpinBox* m_trackMedianWindow = nullptr;
    QDoubleSpinBox* m_trackSmoothingAlpha = nullptr;
    QSpinBox* m_trackJumpConfirmations = nullptr;
    QDoubleSpinBox* m_trackJumpEdgeChangeRatio = nullptr;
    QDoubleSpinBox* m_trackJumpCenterToleranceRatio = nullptr;
    QDoubleSpinBox* m_trackJumpBandwidthToleranceRatio = nullptr;
    QCheckBox* m_channelAggregationEnabled = nullptr;
    FrequencySpinBox* m_channelMaximumBandwidth = nullptr;
    QDoubleSpinBox* m_channelHighThreshold = nullptr;
    QDoubleSpinBox* m_channelLowThreshold = nullptr;
    QDoubleSpinBox* m_channelMinimumSupport = nullptr;
    QDoubleSpinBox* m_channelMinimumCoverage = nullptr;
    QSpinBox* m_channelMergeConfirmations = nullptr;
    QSpinBox* m_channelSplitConfirmations = nullptr;
    QSpinBox* m_channelMissingConfirmations = nullptr;
    QDoubleSpinBox* m_channelMissingHold = nullptr;
    QDoubleSpinBox* m_channelHistorySeconds = nullptr;
    QTableWidget* m_channelPriorTable = nullptr;
    QSpinBox* m_maxSignals = nullptr;
    QLabel* m_feedback = nullptr;
};

} // namespace scn::app
