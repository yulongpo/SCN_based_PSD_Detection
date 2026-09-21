#include "SignalAlarmProcessor.h"
#include "ComStdAfx.h"

SignalAlarmProcessor::SignalAlarmProcessor()
{
}

SignalAlarmProcessor::~SignalAlarmProcessor()
{
}

int SignalAlarmProcessor::addRule(const SignalAlarmRule& rule)
{
    RuleInternal internal;
    internal.rule_id = _next_rule_id++;
    internal.name = rule.name;
    internal.enable = rule.enable;
    internal.bgn_freq = rule.bgn_freq;
    internal.end_freq = rule.end_freq;
    internal.carry_type = rule.carry_type;
    internal.alarm_level = rule.alarm_level;
    internal.sig_min_bw = rule.sig_min_bw;
    internal.sig_max_bw = rule.sig_max_bw;
    internal.note = rule.note;

    _rules.push_back(internal);
    LOG_INFO("SignalAlarmProcessor::addRule rule_id=%lld, name=%s", internal.rule_id, internal.name.c_str());
    return 0;
}

int SignalAlarmProcessor::deleteRule(int64_t rule_id)
{
    for (auto it = _rules.begin(); it != _rules.end(); ++it)
    {
        if (it->rule_id == rule_id)
        {
            _rules.erase(it);
            LOG_INFO("SignalAlarmProcessor::deleteRule rule_id=%lld", rule_id);
            return 0;
        }
    }
    LOG_WARN("SignalAlarmProcessor::deleteRule rule_id=%lld not found.", rule_id);
    return -1;
}

int SignalAlarmProcessor::updateRule(const SignalAlarmRule& rule)
{
    for (auto& r : _rules)
    {
        if (r.rule_id == rule.rule_id)
        {
            r.name = rule.name;
            r.enable = rule.enable;
            r.bgn_freq = rule.bgn_freq;
            r.end_freq = rule.end_freq;
            r.carry_type = rule.carry_type;
            r.alarm_level = rule.alarm_level;
            r.sig_min_bw = rule.sig_min_bw;
            r.sig_max_bw = rule.sig_max_bw;
            r.note = rule.note;
            LOG_INFO("SignalAlarmProcessor::updateRule rule_id=%lld", rule.rule_id);
            return 0;
        }
    }
    LOG_WARN("SignalAlarmProcessor::updateRule rule_id=%lld not found.", rule.rule_id);
    return -1;
}

void SignalAlarmProcessor::clearRules()
{
    _rules.clear();
    _next_rule_id = 1;
}

SignalAlarmRules* SignalAlarmProcessor::getAllRules() const
{
    SignalAlarmRules* result = new SignalAlarmRules();
    result->num = static_cast<int32_t>(_rules.size());
    result->item = new SignalAlarmRule[result->num];

    for (int32_t i = 0; i < result->num; i++)
    {
        SignalAlarmRule& out = result->item[i];
        const RuleInternal& r = _rules[i];
        out.rule_id = r.rule_id;
        size_t name_len = r.name.size() < sizeof(out.name) - 1 ? r.name.size() : sizeof(out.name) - 1;
        memcpy(out.name, r.name.c_str(), name_len);
        out.name[name_len] = '\0';
        out.enable = r.enable;
        out.bgn_freq = r.bgn_freq;
        out.end_freq = r.end_freq;
        out.carry_type = r.carry_type;
        out.alarm_level = r.alarm_level;
        out.sig_min_bw = r.sig_min_bw;
        out.sig_max_bw = r.sig_max_bw;
        size_t note_len = r.note.size() < sizeof(out.note) - 1 ? r.note.size() : sizeof(out.note) - 1;
        memcpy(out.note, r.note.c_str(), note_len);
        out.note[note_len] = '\0';
    }
    return result;
}

int SignalAlarmProcessor::checkAlarms(HQSigMF::DetectionObject* objects, int32_t num_objects)
{
    int match_count = 0;
    for (int32_t i = 0; i < num_objects; i++)
    {
        auto& obj = objects[i];
        int64_t obj_bw = obj.f_end_hz - obj.f_start_hz;
        if (obj_bw < 0) obj_bw = 0;

        for (auto& rule : _rules)
        {
            if (!rule.enable) continue;

            if (obj.f_end_hz < rule.bgn_freq || obj.f_start_hz > rule.end_freq)
                continue;

            if (obj_bw < rule.sig_min_bw || obj_bw > rule.sig_max_bw)
                continue;

            //obj.carry_type = static_cast<int8_t>(rule.carry_type);
            obj.alarm_level = static_cast<int8_t>(rule.alarm_level);

            LOG_DEBUG("SignalAlarmProcessor::checkAlarms ALARM: object[%d] freq=[%lld,%lld] matched rule[%lld] level=%d",
                obj.id, obj.f_start_hz, obj.f_end_hz, rule.rule_id, rule.alarm_level);
            match_count++;
            break;
        }
    }
    return match_count;
}

int SignalAlarmProcessor::loadFromFile(const std::string& filepath)
{
    std::vector<RuleData> loaded;
    int64_t max_id = 1;

    if (!JsonRuleLoader::loadRules(filepath, loaded, max_id))
    {
        LOG_WARN("SignalAlarmProcessor::loadFromFile no rules loaded from %s (file may not exist).", filepath.c_str());
        return -1;
    }

    _rules.clear();
    _rules.reserve(loaded.size());
    _next_rule_id = max_id;

    for (auto& rd : loaded)
    {
        RuleInternal internal;
        internal.rule_id = rd.rule_id;
        internal.name = rd.name;
        internal.enable = rd.enable;
        internal.bgn_freq = rd.bgn_freq;
        internal.end_freq = rd.end_freq;
        internal.carry_type = rd.carry_type;
        internal.alarm_level = rd.alarm_level;
        internal.sig_min_bw = rd.sig_min_bw;
        internal.sig_max_bw = rd.sig_max_bw;
        internal.note = rd.note;
        _rules.push_back(internal);
    }

    LOG_INFO("SignalAlarmProcessor::loadFromFile loaded %d rules from %s.",
        static_cast<int>(_rules.size()), filepath.c_str());
    return 0;
}

int SignalAlarmProcessor::saveToFile(const std::string& filepath) const
{
    std::vector<RuleData> data;
    data.reserve(_rules.size());

    for (auto& r : _rules)
    {
        RuleData rd;
        rd.rule_id = r.rule_id;
        rd.name = r.name;
        rd.enable = r.enable;
        rd.bgn_freq = r.bgn_freq;
        rd.end_freq = r.end_freq;
        rd.carry_type = r.carry_type;
        rd.alarm_level = r.alarm_level;
        rd.sig_min_bw = r.sig_min_bw;
        rd.sig_max_bw = r.sig_max_bw;
        rd.note = r.note;
        data.push_back(rd);
    }

    if (!JsonRuleLoader::saveRules(filepath, data))
    {
        LOG_ERROR("SignalAlarmProcessor::saveToFile failed to write %s.", filepath.c_str());
        return -1;
    }

    LOG_INFO("SignalAlarmProcessor::saveToFile saved %d rules to %s.",
        static_cast<int>(data.size()), filepath.c_str());
    return 0;
}
