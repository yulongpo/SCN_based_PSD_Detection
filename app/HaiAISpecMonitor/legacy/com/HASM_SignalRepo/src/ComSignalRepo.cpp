#include "ComSignalRepo.h"
#include "../core/sqlite/sqlite_wrapper.hpp"
#include <iomanip>
#include <sstream>
#include <ctime>
#include <chrono>
#include <cstring>
#include <memory>

RAICOM_EXPORT(ComSignalRepo);

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
 * @brief 提取由有效 CONFIRMED 跟踪记录引用的当前检测对象。
 *
 * 返回 false 表示没有 TRACKING_RESULT，供旧输入保持原有全量持久化行为；返回
 * true 表示跟踪协议存在，即使段损坏或没有确认对象也必须按空正式结果处理。
 */
bool collectConfirmedObjects(
    HQSigMF* signal,
    const HQSigMF::DetectionObject* objects,
    size_t object_count,
    std::vector<HQSigMF::DetectionObject>& confirmed_objects)
{
    confirmed_objects.clear();
    if (!signal) return false;

    auto section = signal->getSection(
        Domain::FEATURE,
        static_cast<int32_t>(FeatureCategory::TRACKING_RESULT));
    if (!section) return false;

    if (section->property.data_type != DataType::OBJ)
    {
        LOG_WARN("ComSignalRepo::process invalid TRACKING_RESULT data type, suppress tracked objects.");
        return true;
    }

    const auto tracking = section->getData<HQSigMF::TrackingResult>();
    if (!tracking.first || !objects || object_count == 0) return true;

    std::vector<uint8_t> selected(object_count, 0);
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
        if (objects[object_index].id == item.track_id)
            selected[object_index] = 1;
    }

    confirmed_objects.reserve(object_count);
    for (size_t i = 0; i < object_count; ++i)
    {
        if (selected[i] != 0) confirmed_objects.push_back(objects[i]);
    }
    return true;
}
}

static int64_t stringTimeToEpoch(const std::string& datetime)
{
    if (datetime.empty()) return 0;
    std::tm tm = {};
    std::istringstream ss(datetime);
    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    if (ss.fail()) return 0;
    auto tp = std::chrono::system_clock::from_time_t(std::mktime(&tm));
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        tp.time_since_epoch()).count();
}

static void RecordToInfo(const SpectrumFileInfoRecord& rec, SpectrumFileInfo& info)
{
    info.file_id = rec.id;
    strncpy(info.file_name, rec.file_name.c_str(), sizeof(info.file_name) - 1);
    strncpy(info.source_name, rec.device.c_str(), sizeof(info.source_name) - 1);
    info.fc = static_cast<int64_t>((rec.start_freq_mhz + rec.end_freq_mhz) / 2.0 * 1e6);
    info.span = static_cast<int64_t>((rec.end_freq_mhz - rec.start_freq_mhz) * 1e6);
    info.rbw = static_cast<int32_t>(rec.rbw_hz);
    info.ref_level = rec.ref_level;
    info.bgn_time = rec.start_time_ns;
    info.end_time = rec.end_time_ns;
    info.size = static_cast<int32_t>(rec.file_size / (1024 * 1024));
    info.signal_num = rec.signal_count;
    info.alarm_num = rec.alarm_count;
    info.len_per_spec = rec.len_per_spec;
    info.spec_num = rec.spec_num;
}

ComSignalRepo::ComSignalRepo()
{
}

ComSignalRepo::~ComSignalRepo()
{
    if (_core)
    {
        delete _core;
        _core = nullptr;
    }
}

int ComSignalRepo::init()
{
    //状态上报
    reportStatus(radioai::core::EComPhase::INITIALIZE, radioai::core::EComState::BEGIN);

    if (_inited) return 0;
    _core = new (std::nothrow) SignalRepoCore;
    if (!_core)
    {
        LOG_ERROR("ComSignalRepo::init create SignalRepoCore failed.");
        return -1;
    }

    if (0 != _core->init(_spectrum_dir, _db_path))
    {
        LOG_ERROR("ComSignalRepo::init SignalRepoCore init failed.");
        delete _core;
        _core = nullptr;
        return -1;
    }

    //状态上报
    reportStatus(radioai::core::EComPhase::INITIALIZE, radioai::core::EComState::SUCCESS);

    _inited = true;
    LOG_INFO("ComSignalRepo::init success.");
    return 0;
}

int ComSignalRepo::start()
{
    if (_core) _core->markNewFile();
    return 0;
}

void ComSignalRepo::stop()
{
    reset();
}

void ComSignalRepo::resume()
{
    if (_core) _core->markNewFile();
}

int ComSignalRepo::reset()
{
    if (!_core)
    {
        LOG_WARN("ComSignalRepo::reset core not initialized.");
        return -1;
    }

    // 输出当前文件列表
    auto all_infos = _core->getAllFileInfos();
    if (!all_infos.empty())
    {
        auto out = new SpectrumFileInfos();
        out->num = static_cast<int32_t>(all_infos.size());
        out->items = new SpectrumFileInfo[out->num];
        for (int32_t i = 0; i < out->num; i++)
            RecordToInfo(all_infos[i], out->items[i]);
        output(E_OPORT_SPECTRUM_FILE_INFOS, out);
    }

    LOG_INFO("ComSignalRepo::reset success.");
    return 0;
}

int ComSignalRepo::checkParam(const std::map<std::string, std::string>& params)
{
    for (auto& pair : params)
    {
        if (!pair.first.compare("spectrum_dir"))
        {
            if (pair.second.empty())
            {
                LOG_ERROR("ComSignalRepo::checkParam spectrum_dir is empty.");
                return -1;
            }
        }
        else if (!pair.first.compare("file_size"))
        {
            if (!param_check::is_integer(pair.second) || std::stoi(pair.second) <= 0)
            {
                LOG_ERROR("ComSignalRepo::checkParam file_size must be positive integer.");
                return -1;
            }
        }
    }
    return 0;
}

int ComSignalRepo::updateParam(const std::map<std::string, std::string>& params)
{
    for (auto& pair : params)
    {
        if (!pair.first.compare("repo_dir"))
        {
            _spectrum_dir = pair.second + "/spectrum_data";
            _db_path = pair.second + "/signal.db";
        }
        else if (!pair.first.compare("file_size"))
        {
            _file_size = std::stoi(pair.second);
        }
    }

    if (!_inited || !_core) return 0;

    if (0 != _core->init(_spectrum_dir, _db_path))
    {
        LOG_ERROR("ComSignalRepo::updateParam SignalRepoCore init failed.");
        return -1;
    }

    auto all_infos = _core->getAllFileInfos();
    if (!all_infos.empty())
    {
        auto out = new SpectrumFileInfos();
        out->num = static_cast<int32_t>(all_infos.size());
        out->items = new SpectrumFileInfo[out->num];
        for (int32_t i = 0; i < out->num; i++)
            RecordToInfo(all_infos[i], out->items[i]);
        output(E_OPORT_SPECTRUM_FILE_INFOS, out);
    }

    return 0;
}

int ComSignalRepo::process(int portId, radioai::core::ComData* data)
{
    if (!_inited || !_core || !data) return -1;

    switch (portId)
    {
    case E_IPORT_HQSIGMF:
    {
        HQSigMF* sig_mf = dynamic_cast<HQSigMF*>(data);
        if (!sig_mf) return -1;

        // 频谱与时间戳始终持久化；目标统计只接收正式确认的跟踪对象。
        // clone() 共享 Section 所有权，从克隆包替换/移除 OBJECTS 不会修改 UI
        // 仍持有的原始数据包。缺少 TRACKING_RESULT 的旧输入保持原有行为。
        std::unique_ptr<HQSigMF> persist_copy;
        HQSigMF* persist_signal = sig_mf;

        const auto objects = sig_mf->getData<HQSigMF::DetectionObject>(Domain::OBJECTS);
        std::vector<HQSigMF::DetectionObject> confirmed_objects;
        const bool has_tracking = collectConfirmedObjects(
            sig_mf, objects.first, objects.second, confirmed_objects);
        const bool provisional = isProvisionalDetection(sig_mf);

        if (provisional || has_tracking)
        {
            persist_copy.reset(static_cast<HQSigMF*>(sig_mf->clone()));
            if (!persist_copy)
            {
                LOG_ERROR("ComSignalRepo::process clone filtered HQSigMF failed.");
                return -1;
            }
            persist_copy->rmvSections(Domain::OBJECTS);
            if (!provisional && !confirmed_objects.empty() &&
                persist_copy->addObjectsSection(
                    static_cast<int32_t>(confirmed_objects.size()),
                    confirmed_objects.data()) != 0)
            {
                LOG_ERROR("ComSignalRepo::process add confirmed OBJECTS section failed.");
                return -1;
            }
            persist_signal = persist_copy.get();
        }

        SpectrumFileInfoRecord file_info;
        if (0 != _core->writeData(*persist_signal, file_info))
        {
            LOG_ERROR("ComSignalRepo::process writeData failed.");
            return -1;
        }

        auto out = new SpectrumFileInfo();
        RecordToInfo(file_info, *out);
        output(E_OPORT_SPECTRUM_FILE_INFO, out);
        break;
    }
    case E_IPORT_SOURCE_DETAIL:
    {
        SourceDetail* param = dynamic_cast<SourceDetail*>(data);
        if (!param) return -1;

        _source_detail = *param;
        _core->setSourceDetail(*param);
        break;
    }
    case E_IPORT_SIGNAL_DETAIL_QUERY_REQ:
    {
        SignalDetailPerFileQueryReq* req = dynamic_cast<SignalDetailPerFileQueryReq*>(data);
        if (!req) return -1;

        auto signals = _core->readDetsigByFileId(req->file_id);
        if (signals.empty())
        {
            LOG_ERROR("ComSignalRepo::process no detsig data for file_id=%lld.", req->file_id);
            return -1;
        }

        auto resp = new SignalDetailPerFileQueryResp();
        resp->file_id = req->file_id;
        resp->signal_num = static_cast<int32_t>(signals.size());
        resp->item = new SignalDetailPerFileQueryResp::SignalItem[resp->signal_num];

        int idx = 0;
        for (auto pair_src : signals)
        {
            auto& src = pair_src.second;
            auto& item = resp->item[idx++];
            item.id = src.id;
            item.fc = src.fc;
            item.bw = src.bw;
            item.carry_type = src.carry_type;
            item.alarm_level = src.alarm_level;
            item.recent_time = src.recent_time;
            item.burst_num = src.burst_num;
            item.avg_duration = src.avg_duration;

            int32_t time_count = static_cast<int32_t>(src.times.size());
            item.times = new SignalDetailPerFileQueryResp::TimeRange[time_count];
            for (int32_t j = 0; j < time_count; j++)
            {
                item.times[j].bgn_time = src.times[j].first;
                item.times[j].end_time = src.times[j].second;
            }
        }

        output(E_OPORT_SIGNAL_DETAIL_QUERY_RESP, resp);
        break;
    }
    case E_IPORT_SPEC_FILE_DEL_REQ:
    {
        SpectrumFileDelReq* req = dynamic_cast<SpectrumFileDelReq*>(data);
        if (!req || req->file_num <= 0 || !req->file_ids) return -1;

        std::vector<sqlite3_int64> ids;
        ids.reserve(req->file_num);
        for (int32_t i = 0; i < req->file_num; i++)
            ids.push_back(req->file_ids[i]);

        auto out = new SpectrumFileDelResp();
        out->success = 0;
        if (0 != _core->deleteFiles(ids))
            out->success = -1;

        int del_success = out->success;
        output(E_0PORT_SPEC_FILE_DEL_RESP, out);

        // 删除成功后刷新文件列表
        if (del_success == 0)
        {
            auto all_infos = _core->getAllFileInfos();
            auto info_list = new SpectrumFileInfos();
            info_list->num = static_cast<int32_t>(all_infos.size());
            info_list->items = new SpectrumFileInfo[info_list->num];
            for (int32_t i = 0; i < info_list->num; i++)
                RecordToInfo(all_infos[i], info_list->items[i]);
            output(E_OPORT_SPECTRUM_FILE_INFOS, info_list);
        }
        break;
    }
    case E_IPORT_SIGNAL_DEL_REQ:
    {
        SignalDetailPerFileDelReq* req = dynamic_cast<SignalDetailPerFileDelReq*>(data);
        if (!req || req->signal_num <= 0 || !req->signal_ids) return -1;

        std::vector<sqlite3_int64> ids;
        ids.reserve(req->signal_num);
        for (int32_t i = 0; i < req->signal_num; i++)
            ids.push_back(req->signal_ids[i]);

        auto out = new SignalDetailPerFileDelResp();
        out->success = 0;
        if (0 != _core->deleteSignals(req->file_id, ids))
            out->success = -1;

        int del_success = out->success;
        output(E_OPORT_SIGNAL_DEL_RESP, out);

        // 删除成功后刷新文件列表的 signal_count / alarm_count
        if (del_success == 0)
        {
            auto all_infos = _core->getAllFileInfos();
            auto info_list = new SpectrumFileInfos();
            info_list->num = static_cast<int32_t>(all_infos.size());
            info_list->items = new SpectrumFileInfo[info_list->num];
            for (int32_t i = 0; i < info_list->num; i++)
                RecordToInfo(all_infos[i], info_list->items[i]);
            output(E_OPORT_SPECTRUM_FILE_INFOS, info_list);
        }
        break;
    }
    default:
        break;
    }

    return 0;
}

int64_t ComSignalRepo::parseTimeToEpoch(const std::string& datetime)
{
    if (datetime.empty()) return 0;

    std::tm tm = {};
    std::istringstream ss(datetime);
    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    if (ss.fail()) return 0;

    auto tp = std::chrono::system_clock::from_time_t(std::mktime(&tm));
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        tp.time_since_epoch()).count();
}
