#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <cstdlib>

struct RuleData
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

namespace JsonRuleLoader {

inline std::string trim(const std::string& s)
{
    size_t start = s.find_first_not_of(" \t\r\n");
    size_t end = s.find_last_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    return s.substr(start, end - start + 1);
}

inline std::string unescape(const std::string& s)
{
    if (s.empty()) return s;
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++)
    {
        if (s[i] == '\\' && i + 1 < s.size())
        {
            if (s[i + 1] == '"') out += '"';
            else if (s[i + 1] == '\\') out += '\\';
            else if (s[i + 1] == 'n') out += '\n';
            else if (s[i + 1] == 't') out += '\t';
            else { out += s[i]; out += s[i + 1]; }
            i++;
        }
        else
        {
            out += s[i];
        }
    }
    return out;
}

inline std::string escape(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++)
    {
        if (s[i] == '"') out += "\\\"";
        else if (s[i] == '\\') out += "\\\\";
        else if (s[i] == '\n') out += "\\n";
        else if (s[i] == '\t') out += "\\t";
        else out += s[i];
    }
    return out;
}

inline bool loadRules(const std::string& filepath, std::vector<RuleData>& rules, int64_t& next_id)
{
    rules.clear();
    std::ifstream file(filepath);
    if (!file.is_open()) return false;

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    next_id = 1;

    size_t pos = 0;
    while (true)
    {
        size_t brace_open = content.find('{', pos);
        if (brace_open == std::string::npos) break;
        size_t brace_close = content.find('}', brace_open);
        if (brace_close == std::string::npos) break;

        std::string obj = content.substr(brace_open, brace_close - brace_open + 1);
        pos = brace_close + 1;

        if (obj.find("\"rule_id\"") == std::string::npos &&
            obj.find("'rule_id'") == std::string::npos)
            continue;

        RuleData rd;
        rd.rule_id = 0;
        rd.enable = 0;
        rd.bgn_freq = 0;
        rd.end_freq = 0;
        rd.carry_type = 0;
        rd.alarm_level = 0;
        rd.sig_min_bw = 0;
        rd.sig_max_bw = 0;

        size_t key_pos = 0;
        while (true)
        {
            size_t colon = obj.find(':', key_pos);
            if (colon == std::string::npos) break;

            size_t key_close = obj.rfind('"', colon - 1);
            if (key_close == std::string::npos || key_close < key_pos) break;
            size_t key_open = obj.rfind('"', key_close - 1);
            if (key_open == std::string::npos || key_open < key_pos) break;

            std::string key = obj.substr(key_open + 1, key_close - key_open - 1);
            key_pos = colon + 1;

            size_t val_start = obj.find_first_not_of(" \t\r\n", key_pos);
            if (val_start == std::string::npos) break;

            bool matched = true;
            if (obj[val_start] == '"')
            {
                size_t val_end = val_start + 1;
                while (val_end < obj.size())
                {
                    if (obj[val_end] == '\\') val_end += 2;
                    else if (obj[val_end] == '"') break;
                    else val_end++;
                }
                std::string str_val = obj.substr(val_start + 1, val_end - val_start - 1);
                str_val = unescape(str_val);
                key_pos = val_end + 1;

                if (key == "name") rd.name = str_val;
                else if (key == "note") rd.note = str_val;
                else matched = false;
            }
            else
            {
                size_t val_end = obj.find_first_of(",}\n\r", val_start);
                if (val_end == std::string::npos) val_end = obj.size();
                std::string num_str = trim(obj.substr(val_start, val_end - val_start));
                key_pos = val_end + 1;

                if (key == "rule_id") rd.rule_id = static_cast<int64_t>(std::atoll(num_str.c_str()));
                else if (key == "enable") rd.enable = std::atoi(num_str.c_str());
                else if (key == "bgn_freq") rd.bgn_freq = static_cast<int64_t>(std::atoll(num_str.c_str()));
                else if (key == "end_freq") rd.end_freq = static_cast<int64_t>(std::atoll(num_str.c_str()));
                else if (key == "carry_type") rd.carry_type = std::atoi(num_str.c_str());
                else if (key == "alarm_level") rd.alarm_level = std::atoi(num_str.c_str());
                else if (key == "sig_min_bw") rd.sig_min_bw = std::atoi(num_str.c_str());
                else if (key == "sig_max_bw") rd.sig_max_bw = std::atoi(num_str.c_str());
                else matched = false;
            }

            if (key == "rule_id" && rd.rule_id >= next_id)
                next_id = rd.rule_id + 1;
        }

        rules.push_back(rd);
    }

    return !rules.empty();
}

inline bool saveRules(const std::string& filepath, const std::vector<RuleData>& rules)
{
    std::ostringstream oss;
    oss << "{\n    \"rules\": [\n";

    for (size_t i = 0; i < rules.size(); i++)
    {
        const auto& r = rules[i];
        oss << "        {\n";
        oss << "            \"rule_id\": " << r.rule_id << ",\n";
        oss << "            \"name\": \"" << escape(r.name) << "\",\n";
        oss << "            \"enable\": " << r.enable << ",\n";
        oss << "            \"bgn_freq\": " << r.bgn_freq << ",\n";
        oss << "            \"end_freq\": " << r.end_freq << ",\n";
        oss << "            \"carry_type\": " << r.carry_type << ",\n";
        oss << "            \"alarm_level\": " << r.alarm_level << ",\n";
        oss << "            \"sig_min_bw\": " << r.sig_min_bw << ",\n";
        oss << "            \"sig_max_bw\": " << r.sig_max_bw << ",\n";
        oss << "            \"note\": \"" << escape(r.note) << "\"\n";
        oss << "        }";
        if (i < rules.size() - 1) oss << ",";
        oss << "\n";
    }

    oss << "    ]\n}\n";

    std::ofstream file(filepath);
    if (!file.is_open()) return false;

    file << oss.str();
    file.close();
    return true;
}

} // namespace JsonRuleLoader
