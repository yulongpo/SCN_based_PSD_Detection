#include "ComSignalAlarm.h"

RAICOM_EXPORT(ComSignalAlarm);

namespace
{
bool isProvisionalDetection(HQSigMF* signal)
{
    if (!signal) return false;

    auto section = signal->getSection(
        Domain::FEATURE,
        static_cast<int32_t>(FeatureCategory::DETECTION_PROGRESS));
    if (!section || section->property.data_type != DataType::OBJ) return false;

    const auto progress = section->getData<HQSigMF::DetectionProgress>();
    return progress.first && progress.second > 0 &&
        progress.first[0].stage == DetectionStage::PROVISIONAL;
}

/**
 * @brief 根据跟踪元数据标记可触发正式告警的对象。
 *
 * 返回 false 表示输入没有 TRACKING_RESULT，调用方应走旧版兼容逻辑；返回 true
 * 表示跟踪协议已生效，此时只有校验通过的 CONFIRMED 记录会被置位。
 */
bool buildConfirmedMask(
    HQSigMF* signal,
    const HQSigMF::DetectionObject* objects,
    size_t object_count,
    std::vector<uint8_t>& confirmed)
{
    confirmed.assign(object_count, 0);
    if (!signal) return false;

    auto section = signal->getSection(
        Domain::FEATURE,
        static_cast<int32_t>(FeatureCategory::TRACKING_RESULT));
    if (!section) return false;

    if (section->property.data_type != DataType::OBJ)
    {
        LOG_WARN("ComSignalAlarm::process invalid TRACKING_RESULT data type, suppress tracked alarms.");
        return true;
    }

    const auto tracking = section->getData<HQSigMF::TrackingResult>();
    if (!tracking.first) return true;

    for (size_t i = 0; i < tracking.second; ++i)
    {
        const auto& item = tracking.first[i];
        if (item.version != 1 ||
            item.record_size < static_cast<int32_t>(sizeof(HQSigMF::TrackingResult)) ||
            item.state != TrackLifeState::CONFIRMED ||
            item.object_index < 0 ||
            static_cast<size_t>(item.object_index) >= object_count)
        {
            continue;
        }

        const size_t object_index = static_cast<size_t>(item.object_index);
        if (objects && objects[object_index].id == item.track_id)
            confirmed[object_index] = 1;
    }
    return true;
}
}

ComSignalAlarm::ComSignalAlarm()
{
}

ComSignalAlarm::~ComSignalAlarm()
{
    if (_processor)
    {
        if (!_rules_json_path.empty())
        {
            _processor->saveToFile(_rules_json_path);
        }
        delete _processor;
        _processor = nullptr;
    }
}

int ComSignalAlarm::init()
{
    //状态上报
    reportStatus(radioai::core::EComPhase::INITIALIZE, radioai::core::EComState::BEGIN);

    _processor = new (std::nothrow) SignalAlarmProcessor;
    if (!_processor)
    {
        LOG_ERROR("ComSignalAlarm::init create SignalAlarmProcessor failed.");
        return -1;
    }

    if (!_rules_json_path.empty())
    {
        _processor->loadFromFile(_rules_json_path);
    }

    reportRules();

    //状态上报
    reportStatus(radioai::core::EComPhase::INITIALIZE, radioai::core::EComState::SUCCESS);

    _inited = true;
    LOG_INFO("ComSignalAlarm::init success.");
    return 0;
}

int ComSignalAlarm::start()
{
    return 0;
}

void ComSignalAlarm::stop()
{
    if (_processor && !_rules_json_path.empty())
    {
        _processor->saveToFile(_rules_json_path);
    }
}

int ComSignalAlarm::reset()
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
    LOG_INFO("ComSignalAlarm::reset success.");
    return 0;
}

int ComSignalAlarm::checkParam(const std::map<std::string, std::string>& params)
{
    for (auto& pair : params)
    {
        if (!pair.first.compare("rules_json_path"))
        {
            if (pair.second.empty())
            {
                LOG_ERROR("ComSignalAlarm::checkParam rules_json_path is empty.");
                return -1;
            }
        }
    }
    return 0;
}

int ComSignalAlarm::updateParam(const std::map<std::string, std::string>& params)
{
    for (auto& pair : params)
    {
        if (!pair.first.compare("rules_json_path"))
        {
            _rules_json_path = pair.second;
        }
    }

    if (!_inited || !_processor) return 0;

    if (!_rules_json_path.empty())
    {
        _processor->loadFromFile(_rules_json_path);
    }

    reportRules();
    return 0;
}

int ComSignalAlarm::process(int portId, radioai::core::ComData* data)
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

        auto objects = sig_mf->getData<HQSigMF::DetectionObject>(Domain::OBJECTS);
        if (objects.first && objects.second > 0)
        {
            int32_t num = static_cast<int32_t>(objects.second);
            std::vector<HQSigMF::DetectionObject> writable_objects(objects.first, objects.first + num);

            std::vector<uint8_t> confirmed;
            const bool has_tracking = buildConfirmedMask(
                sig_mf, objects.first, objects.second, confirmed);
            const bool provisional = isProvisionalDetection(sig_mf);

            if (has_tracking)
            {
                // 跟踪协议存在时采用失败关闭策略：未确认、无对应记录或损坏记录
                // 都不能继承上游告警等级，也不能触发新的正式告警。
                for (int32_t i = 0; i < num; ++i)
                {
                    writable_objects[static_cast<size_t>(i)].alarm_level = 0;
                    if (!provisional && confirmed[static_cast<size_t>(i)] != 0)
                        _processor->checkAlarms(&writable_objects[static_cast<size_t>(i)], 1);
                }
            }
            else if (!provisional)
            {
                // 缺少 TRACKING_RESULT 的旧数据保持原有规则处理行为。
                _processor->checkAlarms(writable_objects.data(), num);
            }
            else
            {
                // 旧版渐进长窗结果只用于实时图形提示，不触发正式告警。
                output(E_OPORT_HQSIGMF, out);
                break;
            }

            out->rmvSections(Domain::OBJECTS);
            if (out->addObjectsSection(num, writable_objects.data()) != 0)
            {
                LOG_ERROR("ComSignalAlarm::process replace OBJECTS section failed.");
                delete out;
                return -1;
            }
        }

        output(E_OPORT_HQSIGMF, out);
        break;
    }
    case E_IPORT_ALARM_RULE_OP_REQ:
    {
        SignalAlarmRuleOpReq* req = dynamic_cast<SignalAlarmRuleOpReq*>(data);
        if (!req) return -1;

        auto resp = new SignalAlarmRuleOpResp();
        resp->success = 0;

        switch (req->op_type)
        {
        case 0:
            if (0 != _processor->addRule(req->rule))
            {
                resp->success = -1;
                LOG_ERROR("ComSignalAlarm::process addRule failed.");
            }
            break;
        case 1:
            if (0 != _processor->deleteRule(req->rule.rule_id))
            {
                resp->success = -1;
                LOG_ERROR("ComSignalAlarm::process deleteRule failed for rule_id=%lld.", req->rule.rule_id);
            }
            break;
        case 2:
            if (0 != _processor->updateRule(req->rule))
            {
                resp->success = -1;
                LOG_ERROR("ComSignalAlarm::process updateRule failed for rule_id=%lld.", req->rule.rule_id);
            }
            break;
        case 3:
            // 查询/刷新规则列表，不做任何修改
            break;
        default:
            resp->success = -1;
            LOG_ERROR("ComSignalAlarm::process unknown op_type=%d.", req->op_type);
            break;
        }

        output(E_OPORT_ALARM_RULE_OP_RESP, resp);
        persistRules();
        reportRules();
        break;
    }
    default:
        break;
    }

    return 0;
}

void ComSignalAlarm::reportRules()
{
    if (!_processor) return;

    SignalAlarmRules* out = _processor->getAllRules();
    output(E_OPORT_ALARM_RULES, out);
}

int ComSignalAlarm::persistRules()
{
    if (_rules_json_path.empty())
    {
        LOG_WARN("ComSignalAlarm::persistRules rules_json_path not set, skip save.");
        return -1;
    }
    return _processor->saveToFile(_rules_json_path);
}
