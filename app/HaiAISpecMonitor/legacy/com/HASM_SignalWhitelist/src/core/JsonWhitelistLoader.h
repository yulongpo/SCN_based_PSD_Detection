#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <cstdlib>

struct WhitelistItemData
{
    int64_t id;
    std::string name;
    int32_t enable;
    int64_t bgn_freq;
    int64_t end_freq;
    std::string note;
};

namespace JsonWhitelistLoader {

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

inline bool loadWhitelists(const std::string& filepath, std::vector<WhitelistItemData>& whitelists, int64_t& next_id)
{
    whitelists.clear();
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

        if (obj.find("\"id\"") == std::string::npos &&
            obj.find("'id'") == std::string::npos)
            continue;

        WhitelistItemData item;
        item.id = 0;
        item.enable = 0;
        item.bgn_freq = 0;
        item.end_freq = 0;

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

                if (key == "name") item.name = str_val;
                else if (key == "note") item.note = str_val;
                else matched = false;
            }
            else
            {
                size_t val_end = obj.find_first_of(",}\n\r", val_start);
                if (val_end == std::string::npos) val_end = obj.size();
                std::string num_str = trim(obj.substr(val_start, val_end - val_start));
                key_pos = val_end + 1;

                if (key == "id") item.id = static_cast<int64_t>(std::atoll(num_str.c_str()));
                else if (key == "enable") item.enable = std::atoi(num_str.c_str());
                else if (key == "bgn_freq") item.bgn_freq = static_cast<int64_t>(std::atoll(num_str.c_str()));
                else if (key == "end_freq") item.end_freq = static_cast<int64_t>(std::atoll(num_str.c_str()));
                else matched = false;
            }

            if (key == "id" && item.id >= next_id)
                next_id = item.id + 1;
        }

        whitelists.push_back(item);
    }

    return !whitelists.empty();
}

inline bool saveWhitelists(const std::string& filepath, const std::vector<WhitelistItemData>& whitelists)
{
    std::ostringstream oss;
    oss << "{\n    \"whitelists\": [\n";

    for (size_t i = 0; i < whitelists.size(); i++)
    {
        const auto& item = whitelists[i];
        oss << "        {\n";
        oss << "            \"id\": " << item.id << ",\n";
        oss << "            \"name\": \"" << escape(item.name) << "\",\n";
        oss << "            \"enable\": " << item.enable << ",\n";
        oss << "            \"bgn_freq\": " << item.bgn_freq << ",\n";
        oss << "            \"end_freq\": " << item.end_freq << ",\n";
        oss << "            \"note\": \"" << escape(item.note) << "\"\n";
        oss << "        }";
        if (i < whitelists.size() - 1) oss << ",";
        oss << "\n";
    }

    oss << "    ]\n}\n";

    std::ofstream file(filepath);
    if (!file.is_open()) return false;

    file << oss.str();
    file.close();
    return true;
}

} // namespace JsonWhitelistLoader
