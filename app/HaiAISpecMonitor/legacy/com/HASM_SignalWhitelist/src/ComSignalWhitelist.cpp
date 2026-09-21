#include "ComSignalWhitelist.h"

RAICOM_EXPORT(ComSignalWhitelist);

ComSignalWhitelist::ComSignalWhitelist()
{
}

ComSignalWhitelist::~ComSignalWhitelist()
{
    if (_processor)
    {
        if (!_whitelist_json_path.empty())
        {
            _processor->saveToFile(_whitelist_json_path);
        }
        delete _processor;
        _processor = nullptr;
    }
}

int ComSignalWhitelist::init()
{
    //状态上报
    reportStatus(radioai::core::EComPhase::INITIALIZE, radioai::core::EComState::BEGIN);

    _processor = new (std::nothrow) SignalWhitelistProcessor;
    if (!_processor)
    {
        LOG_ERROR("ComSignalAlarm::init create SignalAlarmProcessor failed.");
        return -1;
    }

    if (!_whitelist_json_path.empty())
    {
        _processor->loadFromFile(_whitelist_json_path);
    }

    reportWhitelists();

    //状态上报
    reportStatus(radioai::core::EComPhase::INITIALIZE, radioai::core::EComState::SUCCESS);

    _inited = true;
    LOG_INFO("ComSignalAlarm::init success.");
    return 0;
}

int ComSignalWhitelist::start()
{
    return 0;
}

void ComSignalWhitelist::stop()
{
    if (_processor && !_whitelist_json_path.empty())
    {
        _processor->saveToFile(_whitelist_json_path);
    }
}

int ComSignalWhitelist::reset()
{
#if 0
    if (_processor)
    {
        _processor->clearRules();
        if (!_rules_json_path.empty())
        {
            _processor->saveToFile(_rules_json_path);
        }
    }

    reportRules();
#endif
    LOG_INFO("ComSignalWhitelist::reset success.");
    return 0;
}

int ComSignalWhitelist::checkParam(const std::map<std::string, std::string>& params)
{
    for (auto& pair : params)
    {
        if (!pair.first.compare("whitelist_json_path"))
        {
            if (pair.second.empty())
            {
                LOG_ERROR("ComSignalWhitelist::checkParam whitelist_json_path is empty.");
                return -1;
            }
        }
    }
    return 0;
}

int ComSignalWhitelist::updateParam(const std::map<std::string, std::string>& params)
{
    for (auto& pair : params)
    {
        if (!pair.first.compare("whitelist_json_path"))
        {
            _whitelist_json_path = pair.second;
        }
    }

    if (!_inited || !_processor) return 0;

    if (!_whitelist_json_path.empty())
    {
        _processor->loadFromFile(_whitelist_json_path);
    }

    reportWhitelists();
    return 0;
}

int ComSignalWhitelist::process(int portId, radioai::core::ComData* data)
{
    if (!_inited || !_processor || !data) return -1;

    switch (portId)
    {
    case E_IPORT_HQSIGMF:
    {
        HQSigMF* sig_mf = dynamic_cast<HQSigMF*>(data);
        if (!sig_mf) return -1;

        HQSigMF* out = static_cast<HQSigMF*>(sig_mf->clone());
        if (!out) return -1;

        // 每次重新生成白名单匹配元数据，避免克隆包携带上游的旧匹配结果。
        out->rmvSection(
            Domain::FEATURE,
            static_cast<int32_t>(FeatureCategory::WHITELIST_MATCH));

        auto objects = sig_mf->getData<HQSigMF::DetectionObject>(Domain::OBJECTS);
        if (objects.first && objects.second > 0)
        {
            int32_t num = static_cast<int32_t>(objects.second);
            std::vector<HQSigMF::WhitelistMatch> matches(static_cast<size_t>(num));
            if (_processor->checkWhitelists(objects.first, num, matches.data()) < 0 ||
                out->addFeatureDomainSection(
                    num,
                    matches.data(),
                    static_cast<int32_t>(FeatureCategory::WHITELIST_MATCH)) != 0)
            {
                LOG_ERROR("ComSignalWhitelist::process add whitelist metadata failed.");
                delete out;
                return -1;
            }
        }

        output(E_OPORT_HQSIGMF, out);
        break;
    }
    case E_IPORT_WHITELIST_OP_REQ:
    {
        SignalWhitelistOpReq* req = dynamic_cast<SignalWhitelistOpReq*>(data);
        if (!req) return -1;

        auto resp = new SignalWhitelistOpResp();
        resp->success = 0;

        switch (req->op_type)
        {
        case 0:
            if (0 != _processor->addItem(req->item))
            {
                resp->success = -1;
                LOG_ERROR("ComSignalWhitelist::process addItem failed.");
            }
            break;
        case 1:
            if (0 != _processor->deleteItem(req->item.id))
            {
                resp->success = -1;
                LOG_ERROR("ComSignalWhitelist::process deleteItem failed for id=%lld.", req->item.id);
            }
            break;
        case 2:
            if (0 != _processor->updateItem(req->item))
            {
                resp->success = -1;
                LOG_ERROR("ComSignalWhitelist::process updateRule failed for id=%lld.", req->item.id);
            }
            break;
        case 3:
            // 查询/刷新规则列表，不做任何修改
            break;
        default:
            resp->success = -1;
            LOG_ERROR("ComSignalWhitelist::process unknown op_type=%d.", req->op_type);
            break;
        }

        output(E_OPORT_WHITELIST_OP_RESP, resp);
        persistWhitelists();
        reportWhitelists();
        break;
    }
    default:
        break;
    }

    return 0;
}

void ComSignalWhitelist::reportWhitelists()
{
    if (!_processor) return;

    SignalWhitelists* out = _processor->getAllItems();
    output(E_OPORT_WHITELISTS, out);
}

int ComSignalWhitelist::persistWhitelists()
{
    if (_whitelist_json_path.empty())
    {
        LOG_WARN("ComSignalWhitelist::persistWhitelists whitelist_json_path not set, skip save.");
        return -1;
    }
    return _processor->saveToFile(_whitelist_json_path);
}
