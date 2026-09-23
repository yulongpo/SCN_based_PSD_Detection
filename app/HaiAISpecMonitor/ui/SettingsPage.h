#pragma once

#include "../../../algorithm/DetectionConfig.h"
#include "../../../application/RecordingConfig.h"
#include "../../../application/policy/PolicyTypes.h"

#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTableWidget>
#include <QWidget>

class QStackedWidget;
class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;

namespace scn::app
{

class FrequencySpinBox;

namespace policy = scn::application::policy;

/**
 * @brief 原 ISA 系统设置页面的 Qt6 兼容实现。
 *
 * SCN 配置经 MainWindow 提交到会话，接受后才保存；告警、白名单、
 * 存储等其余页面保留既有界面和操作日志。
 */
class SettingsPage final : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsPage(QWidget* parent = nullptr);
    int displayRefreshRateHz() const;
    double displayDynamicRangeDb() const;
    application::RecordingConfig recordingConfig() const;
    void saveRecordingConfig() const;
    void setRecordingStatus(const QString& status);
    algorithm::DetectionConfig detectionConfig() const;
    void acceptDetectionConfig(const algorithm::DetectionConfig& config);
    void setDetectionFeedback(const QString& message);
    policy::PolicyConfig policyConfig(QString* error = nullptr) const;
    bool acceptPolicyConfig(const policy::PolicyConfig& config, QString* error = nullptr);

signals:
    void logMessage(const QString& message);
    void displayRefreshRateChanged(int rateHz);
    void displayDynamicRangeChanged(double rangeDb);
    void temporaryUnobservedMarkersChanged(bool visible);
    void detectionApplyRequested();
    void policyApplyRequested();
    void alarmHistoryRequested();

private slots:
    void selectPage(int index);
    void addRule();
    void removeRule();
    void addWhitelist();
    void removeWhitelist();
    void addChannelPrior();
    void removeChannelPrior();
    void applyPolicy();
    void importPolicy();
    void exportPolicy();
    void applyStorage();
    void clearLog();
    void exportLog();

private:
    void buildUi();
    QWidget* buildDisplayPage();
    QWidget* buildStoragePage();
    QWidget* buildRulePage();
    QWidget* buildWhitelistPage();
    QWidget* buildHistoryPage();
    QWidget* buildPushPage();
    QWidget* buildLogPage();
    QWidget* buildHelpPage();
    QWidget* buildDetectionPage();
    void loadDetectionConfig();
    void setDetectionConfig(const algorithm::DetectionConfig& config);
    void saveDetectionConfig(const algorithm::DetectionConfig& config) const;
    void appendLog(const QString& message);
    void loadPolicyConfig();
    void populatePolicyTables(const policy::PolicyConfig& config);
    bool savePolicyConfig(const policy::PolicyConfig& config, QString* error = nullptr);

    QStackedWidget* m_stack = nullptr;
    QPlainTextEdit* m_logEdit = nullptr;
    QSpinBox* m_displayRate = nullptr;
    QSpinBox* m_displayDynamicRange = nullptr;
    QLineEdit* m_storagePath = nullptr;
    QCheckBox* m_recordingEnabled = nullptr;
    QLineEdit* m_recordingDirectory = nullptr;
    QLabel* m_recordingStatus = nullptr;
    QTableWidget* m_ruleTable = nullptr;
    QTableWidget* m_whitelistTable = nullptr;
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
    QLabel* m_detectionFeedback = nullptr;
};

} // namespace scn::app
