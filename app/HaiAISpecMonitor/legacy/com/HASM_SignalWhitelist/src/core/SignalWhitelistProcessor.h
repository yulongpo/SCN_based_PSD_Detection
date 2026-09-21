#pragma once
#include "SignalWhitelist.hpp"
#include "radioai/icd/HQSigMF.hpp"
#include "core/JsonWhitelistLoader.h"
#include <vector>
#include <string>

class SignalWhitelistProcessor
{
public:
    SignalWhitelistProcessor();
    ~SignalWhitelistProcessor();

    int addItem(const SignalWhitelist& item);
    int deleteItem(int64_t id);
    int updateItem(const SignalWhitelist& rule);
    void clearItems();
    SignalWhitelists* getAllItems() const;
    /**
     * Build one whitelist-match record per detection without changing its
     * measured frequency bounds. The caller owns a num_objects-sized output.
     * Returns the match count, or -1 for invalid arguments.
     */
    int checkWhitelists(
        const HQSigMF::DetectionObject* objects,
        int32_t num_objects,
        HQSigMF::WhitelistMatch* matches) const;

    int loadFromFile(const std::string& filepath);
    int saveToFile(const std::string& filepath) const;

private:
    struct WhitelistInternal
    {
        int64_t id;
        std::string name;
        int32_t enable;
        int64_t bgn_freq;
        int64_t end_freq;
        std::string note;
    };

    std::vector<WhitelistInternal> _items;
    int64_t _next_item_id = 1;
};
