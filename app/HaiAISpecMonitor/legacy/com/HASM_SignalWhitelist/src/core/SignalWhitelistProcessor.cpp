#include "SignalWhitelistProcessor.h"
#include "ComStdAfx.h"

SignalWhitelistProcessor::SignalWhitelistProcessor()
{
}

SignalWhitelistProcessor::~SignalWhitelistProcessor()
{
}

int SignalWhitelistProcessor::addItem(const SignalWhitelist& item)
{
    WhitelistInternal internal;
    internal.id = _next_item_id++;
    internal.name = item.name;
    internal.enable = item.enable;
    internal.bgn_freq = item.bgn_freq;
    internal.end_freq = item.end_freq;
    internal.note = item.note;

    _items.push_back(internal);
    LOG_INFO("SignalWhitelistProcessor::addItem id=%lld, name=%s", internal.id, internal.name.c_str());
    return 0;
}

int SignalWhitelistProcessor::deleteItem(int64_t id)
{
    for (auto it = _items.begin(); it != _items.end(); ++it)
    {
        if (it->id == id)
        {
            _items.erase(it);
            LOG_INFO("SignalWhitelistProcessor::deleteItem id=%lld", id);
            return 0;
        }
    }
    LOG_WARN("SignalWhitelistProcessor::deleteItem id=%lld not found.", id);
    return -1;
}

int SignalWhitelistProcessor::updateItem(const SignalWhitelist& item)
{
    for (auto& repo_item : _items)
    {
        if (repo_item.id == item.id)
        {
            repo_item.name = item.name;
            repo_item.enable = item.enable;
            repo_item.bgn_freq = item.bgn_freq;
            repo_item.end_freq = item.end_freq;
            repo_item.note = item.note;
            LOG_INFO("SignalAlarmProcessor::updateItem id=%lld", item.id);
            return 0;
        }
    }
    LOG_WARN("SignalAlarmProcessor::updateItem id=%lld not found.", item.id);
    return -1;
}

void SignalWhitelistProcessor::clearItems()
{
    _items.clear();
    _next_item_id = 1;
}

SignalWhitelists* SignalWhitelistProcessor::getAllItems() const
{
    SignalWhitelists* result = new SignalWhitelists();
    result->num = static_cast<int32_t>(_items.size());
    result->item = new SignalWhitelist[result->num];

    for (int32_t i = 0; i < result->num; i++)
    {
        SignalWhitelist& out = result->item[i];
        const WhitelistInternal& r = _items[i];
        out.id = r.id;
        size_t name_len = r.name.size() < sizeof(out.name) - 1 ? r.name.size() : sizeof(out.name) - 1;
        memcpy(out.name, r.name.c_str(), name_len);
        out.name[name_len] = '\0';
        out.enable = r.enable;
        out.bgn_freq = r.bgn_freq;
        out.end_freq = r.end_freq;
        size_t note_len = r.note.size() < sizeof(out.note) - 1 ? r.note.size() : sizeof(out.note) - 1;
        memcpy(out.note, r.note.c_str(), note_len);
        out.note[note_len] = '\0';
    }
    return result;
}

int SignalWhitelistProcessor::checkWhitelists(
    const HQSigMF::DetectionObject* objects,
    int32_t num_objects,
    HQSigMF::WhitelistMatch* matches) const
{
    if (!objects || !matches || num_objects < 0) return -1;

    int match_count = 0;
    for (int32_t i = 0; i < num_objects; i++)
    {
        const auto& obj = objects[i];
        auto& match = matches[i];
        match.version = 1;
        match.record_size = static_cast<int32_t>(sizeof(HQSigMF::WhitelistMatch));
        match.object_index = i;
        match.matched = 0;
        match.track_id = obj.id;
        match.whitelist_id = 0;
        match.canonical_start_hz = obj.f_start_hz;
        match.canonical_end_hz = obj.f_end_hz;

        for (const auto& item : _items)
        {
            if (!item.enable) continue;

            if (obj.f_end_hz < item.bgn_freq || obj.f_start_hz > item.end_freq)
                continue;

            match.matched = 1;
            match.whitelist_id = item.id;
            match.canonical_start_hz = item.bgn_freq;
            match.canonical_end_hz = item.end_freq;

            LOG_DEBUG("SignalWhitelistProcessor::checkWhitelists object[%lld] measured=[%lld,%lld] matched whitelist[%lld] canonical=[%lld,%lld]",
                obj.id, obj.f_start_hz, obj.f_end_hz, item.id, item.bgn_freq, item.end_freq);
            match_count++;
            break;
        }
    }
    return match_count;
}

int SignalWhitelistProcessor::loadFromFile(const std::string& filepath)
{
    std::vector<WhitelistItemData> loaded;
    int64_t max_id = 1;

    if (!JsonWhitelistLoader::loadWhitelists(filepath, loaded, max_id))
    {
        LOG_WARN("SignalAlarmProcessor::loadFromFile no rules loaded from %s (file may not exist).", filepath.c_str());
        return -1;
    }

    _items.clear();
    _items.reserve(loaded.size());
    _next_item_id = max_id;

    for (auto& rd : loaded)
    {
        WhitelistInternal internal;
        internal.id = rd.id;
        internal.name = rd.name;
        internal.enable = rd.enable;
        internal.bgn_freq = rd.bgn_freq;
        internal.end_freq = rd.end_freq;
        internal.note = rd.note;
        _items.push_back(internal);
    }

    LOG_INFO("SignalWhitelistProcessor::loadFromFile loaded %d rules from %s.",
        static_cast<int>(_items.size()), filepath.c_str());
    return 0;
}

int SignalWhitelistProcessor::saveToFile(const std::string& filepath) const
{
    std::vector<WhitelistItemData> data;
    data.reserve(_items.size());

    for (auto& item : _items)
    {
        WhitelistItemData save_item;
        save_item.id = item.id;
        save_item.name = item.name;
        save_item.enable = item.enable;
        save_item.bgn_freq = item.bgn_freq;
        save_item.end_freq = item.end_freq;
        save_item.note = item.note;
        data.push_back(save_item);
    }

    if (!JsonWhitelistLoader::saveWhitelists(filepath, data))
    {
        LOG_ERROR("SignalWhitelistProcessor::saveToFile failed to write %s.", filepath.c_str());
        return -1;
    }

    LOG_INFO("SignalWhitelistProcessor::saveToFile saved %d rules to %s.",
        static_cast<int>(data.size()), filepath.c_str());
    return 0;
}
