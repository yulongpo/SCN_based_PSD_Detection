#pragma once
#include "radioai/icd/SignalData.hpp"
#include "radioai/icd/HQSigMF.hpp"
#include "core/JsonRuleLoader.h"
#include <vector>
#include <string>

class SignalAlarmProcessor
{
public:
    SignalAlarmProcessor();
    ~SignalAlarmProcessor();

    int addRule(const SignalAlarmRule& rule);
    int deleteRule(int64_t rule_id);
    int updateRule(const SignalAlarmRule& rule);
    void clearRules();
    SignalAlarmRules* getAllRules() const;
    int checkAlarms(HQSigMF::DetectionObject* objects, int32_t num_objects);

    int loadFromFile(const std::string& filepath);
    int saveToFile(const std::string& filepath) const;

private:
    struct RuleInternal
    {
        int64_t rule_id;
        std::string name;
        int32_t enable;
        int64_t bgn_freq;
        int64_t end_freq;
        int32_t carry_type;
        int32_t alarm_level;
        int32_t sig_min_bw;
        int32_t sig_max_bw;
        std::string note;
    };

    std::vector<RuleInternal> _rules;
    int64_t _next_rule_id = 1;
};
