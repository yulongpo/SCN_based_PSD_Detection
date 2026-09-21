#include "PolicyRepository.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>
#include <cmath>
#include <set>

namespace scn::application::policy
{

namespace
{
QString applicationConfigDirectory()
{
    const auto base = QCoreApplication::instance()
        ? QCoreApplication::applicationDirPath() : QDir::currentPath();
    const auto dir = QDir(base).filePath(QStringLiteral("config"));
    QDir().mkpath(dir);
    return dir;
}

QJsonObject whitelistToJson(const WhitelistEntry& item)
{
    return {{"id", item.id}, {"name", QString::fromStdString(item.name)},
        {"enabled", item.enabled}, {"startFrequencyHz", item.startFrequencyHz},
        {"endFrequencyHz", item.endFrequencyHz}, {"note", QString::fromStdString(item.note)}};
}

QJsonObject ruleToJson(const AlarmRule& rule)
{
    return {{"id", rule.id}, {"name", QString::fromStdString(rule.name)},
        {"enabled", rule.enabled}, {"startFrequencyHz", rule.startFrequencyHz},
        {"endFrequencyHz", rule.endFrequencyHz}, {"minBandwidthHz", rule.minBandwidthHz},
        {"maxBandwidthHz", rule.maxBandwidthHz}, {"useMinSignalLevel", rule.useMinSignalLevel},
        {"minSignalLevelDbm", rule.minSignalLevelDbm}, {"useMinCnr", rule.useMinCnr},
        {"minCnrDb", rule.minCnrDb}, {"useMinConfidence", rule.useMinConfidence},
        {"minConfidence", rule.minConfidence}, {"level", static_cast<int>(rule.level)},
        {"consecutiveHits", static_cast<int>(rule.consecutiveHits)},
        {"minDurationSeconds", rule.minDurationSeconds},
        {"clearDelaySeconds", rule.clearDelaySeconds},
        {"note", QString::fromStdString(rule.note)}};
}

double number(const QJsonObject& object, const char* key, double fallback = 0.0)
{
    const auto value = object.value(QLatin1String(key));
    return value.isDouble() ? value.toDouble() : fallback;
}

bool readRange(const QJsonObject& object, double& start, double& end)
{
    start = number(object, "startFrequencyHz", number(object, "bgn_freq"));
    end = number(object, "endFrequencyHz", number(object, "end_freq"));
    return std::isfinite(start) && std::isfinite(end) && start < end;
}
}

std::string PolicyRepository::defaultPath()
{
    const auto dir = applicationConfigDirectory();
    return QDir(dir).filePath(QStringLiteral("policy.json")).toStdString();
}

std::string PolicyRepository::historyPath()
{
    const auto dir = applicationConfigDirectory();
    return QDir(dir).filePath(QStringLiteral("policy.sqlite")).toStdString();
}

bool PolicyRepository::validate(const PolicyConfig& config, std::string& error)
{
    std::set<std::int64_t> whitelistIds;
    for (const auto& item : config.whitelists) {
        if (item.id <= 0 || !whitelistIds.insert(item.id).second) {
            error = "白名单 ID 必须为正数且不能重复。"; return false;
        }
        if (!std::isfinite(item.startFrequencyHz) || !std::isfinite(item.endFrequencyHz) ||
            item.startFrequencyHz >= item.endFrequencyHz) {
            error = "白名单频率范围无效。"; return false;
        }
    }
    std::set<std::int64_t> ruleIds;
    for (const auto& rule : config.alarmRules) {
        if (rule.id <= 0 || !ruleIds.insert(rule.id).second) {
            error = "告警规则 ID 必须为正数且不能重复。"; return false;
        }
        if (!std::isfinite(rule.startFrequencyHz) || !std::isfinite(rule.endFrequencyHz) ||
            rule.startFrequencyHz >= rule.endFrequencyHz || rule.consecutiveHits == 0 ||
            !std::isfinite(rule.minBandwidthHz) || !std::isfinite(rule.maxBandwidthHz) ||
            !std::isfinite(rule.minDurationSeconds) || !std::isfinite(rule.clearDelaySeconds) ||
            rule.minDurationSeconds < 0 || rule.clearDelaySeconds < 0 ||
            (rule.level != AlarmLevel::General && rule.level != AlarmLevel::Critical) ||
            rule.minBandwidthHz < 0 || (rule.maxBandwidthHz > 0 && rule.maxBandwidthHz < rule.minBandwidthHz) ||
            (rule.useMinConfidence && (!std::isfinite(rule.minConfidence) || rule.minConfidence < 0 || rule.minConfidence > 1)) ||
            (rule.useMinSignalLevel && !std::isfinite(rule.minSignalLevelDbm)) ||
            (rule.useMinCnr && !std::isfinite(rule.minCnrDb))) {
            error = "告警规则参数无效。"; return false;
        }
    }
    return true;
}

bool PolicyRepository::load(const std::string& path, PolicyConfig& config, std::string& error)
{
    QFile file(QString::fromStdString(path));
    if (!file.exists()) { config = {}; config.version = 1; return true; }
    if (!file.open(QIODevice::ReadOnly)) { error = file.errorString().toStdString(); return false; }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        error = parseError.errorString().toStdString(); return false;
    }
    const auto root = document.object();
    config = {};
    config.version = static_cast<std::uint64_t>(root.value("version").toInteger(1));
    const auto whitelists = root.value("whitelists").toArray();
    std::int64_t nextWhitelistId = 1;
    for (const auto& value : whitelists) {
        const auto object = value.toObject(); WhitelistEntry item;
        item.id = static_cast<std::int64_t>(object.value("id").toInteger());
        if (item.id <= 0) item.id = nextWhitelistId;
        nextWhitelistId = std::max(nextWhitelistId, item.id + 1);
        item.name = object.value("name").toString().toStdString();
        item.enabled = object.value("enabled").toBool(object.value("enable").toInt(1) != 0);
        item.startFrequencyHz = number(object, "startFrequencyHz", number(object, "bgn_freq"));
        item.endFrequencyHz = number(object, "endFrequencyHz", number(object, "end_freq"));
        item.note = object.value("note").toString().toStdString();
        config.whitelists.push_back(std::move(item));
    }
    const auto rules = root.value("alarmRules").isArray()
        ? root.value("alarmRules").toArray() : root.value("rules").toArray();
    std::int64_t nextRuleId = 1;
    for (const auto& value : rules) {
        const auto object = value.toObject(); AlarmRule rule;
        rule.id = static_cast<std::int64_t>(object.value("id").toInteger(object.value("rule_id").toInteger()));
        if (rule.id <= 0) rule.id = nextRuleId;
        nextRuleId = std::max(nextRuleId, rule.id + 1);
        rule.name = object.value("name").toString().toStdString();
        rule.enabled = object.value("enabled").toBool(object.value("enable").toInt(1) != 0);
        if (!readRange(object, rule.startFrequencyHz, rule.endFrequencyHz)) {
            error = "告警规则频率范围无效。"; return false;
        }
        rule.minBandwidthHz = number(object, "minBandwidthHz", number(object, "sig_min_bw"));
        rule.maxBandwidthHz = number(object, "maxBandwidthHz", number(object, "sig_max_bw"));
        rule.useMinSignalLevel = object.value("useMinSignalLevel").toBool(false);
        rule.minSignalLevelDbm = static_cast<float>(number(object, "minSignalLevelDbm"));
        rule.useMinCnr = object.value("useMinCnr").toBool(false);
        rule.minCnrDb = static_cast<float>(number(object, "minCnrDb"));
        rule.useMinConfidence = object.value("useMinConfidence").toBool(false);
        rule.minConfidence = static_cast<float>(number(object, "minConfidence"));
        rule.level = static_cast<AlarmLevel>(object.value("level").toInt(object.value("alarm_level").toInt(1)));
        rule.consecutiveHits = static_cast<std::uint32_t>(object.value("consecutiveHits").toInt(1));
        rule.minDurationSeconds = number(object, "minDurationSeconds");
        rule.clearDelaySeconds = number(object, "clearDelaySeconds", 1.0);
        rule.note = object.value("note").toString().toStdString();
        config.alarmRules.push_back(std::move(rule));
    }
    return validate(config, error);
}

bool PolicyRepository::save(const std::string& path, const PolicyConfig& config, std::string& error)
{
    if (!validate(config, error)) return false;
    QJsonObject root;
    root.insert("version", static_cast<qint64>(config.version));
    QJsonArray whitelists; for (const auto& item : config.whitelists) whitelists.append(whitelistToJson(item));
    QJsonArray rules; for (const auto& rule : config.alarmRules) rules.append(ruleToJson(rule));
    root.insert("whitelists", whitelists); root.insert("alarmRules", rules);
    QSaveFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0 ||
        !file.commit()) { error = file.errorString().toStdString(); return false; }
    return true;
}

} // namespace scn::application::policy
