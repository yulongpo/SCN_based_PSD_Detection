#include "SignalRepoCore.h"
#include <direct.h>
#include <errno.h>
#include <io.h>
#include "utils.hpp"

// ====== DetectedSignalStore ======

void DetectedSignalStore::addOrUpdate(const HQSigMF::DetectionObject& obj, int64_t timestamp_ms)
{
    int64_t fc_hz = (static_cast<int64_t>(obj.f_start_hz) + static_cast<int64_t>(obj.f_end_hz)) / 2;
    int32_t bw_hz = static_cast<int32_t>(obj.f_end_hz - obj.f_start_hz);
    int64_t duration_ms = static_cast<int64_t>(obj.t_end_ns - obj.t_start_ns) / NS_PER_MS;

    auto it = _signals.find(obj.id);
    if (it != _signals.end())
    {
        auto& sig = it->second;

        // ID 表示身份，中心频率和带宽表示本次确认量测；二者必须解耦，
        // 使同一轨迹可以实时反映带宽扩缩而不创建新记录。
        sig.fc = fc_hz;
        sig.bw = bw_hz;

        auto& time_sec = sig.times.rbegin();
        
        //起始时间相同
        if (time_sec->first == obj.t_start_ns) 
        {
            sig.sum_duration += (obj.t_end_ns - time_sec->second);
            time_sec->second = obj.t_end_ns;
            
        }else
        {
            sig.times.push_back({ obj.t_start_ns, obj.t_end_ns });
            sig.burst_num++;

            sig.carry_type = 1;

            sig.sum_duration += (obj.t_end_ns - obj.t_start_ns);
        }

        sig.recent_time = obj.t_end_ns;

        sig.avg_duration = sig.sum_duration / sig.burst_num;
    }
    else
    {
        DetectedSignalRecord sig;
        sig.id = obj.id;
        sig.fc = fc_hz;
        sig.bw = bw_hz;
        sig.carry_type = obj.carry_type;
        sig.alarm_level = obj.alarm_level;
        sig.recent_time = obj.t_end_ns;
        sig.burst_num = 1;
        sig.avg_duration = duration_ms;
        sig.sum_duration = duration_ms;
        sig.times.push_back({ obj.t_start_ns, obj.t_end_ns });
        _signals[obj.id] = std::move(sig);

        //_fc_to_idx[fc_hz] = _signals.size();
        //_signals.push_back(std::move(sig));
    }
}

void DetectedSignalStore::clear()
{
    _signals.clear();
    _fc_to_idx.clear();
    _next_id = 1;
}

int DetectedSignalStore::writeToFile(const std::string& filePath,
    const std::map<int64_t, DetectedSignalRecord>& signals, int64_t file_id)
{
    std::ofstream ofs(filePath, std::ios::binary);
    if (!ofs.is_open()) return -1;

    int32_t signal_num = static_cast<int32_t>(signals.size());

    ofs.write(reinterpret_cast<const char*>(&file_id), sizeof(file_id));
    ofs.write(reinterpret_cast<const char*>(&signal_num), sizeof(signal_num));

    for (const auto& map_sig : signals)
    {
        const auto& sig = map_sig.second;
        int32_t time_count = static_cast<int32_t>(sig.times.size());

        ofs.write(reinterpret_cast<const char*>(&sig.id), sizeof(sig.id));
        ofs.write(reinterpret_cast<const char*>(&sig.fc), sizeof(sig.fc));
        ofs.write(reinterpret_cast<const char*>(&sig.bw), sizeof(sig.bw));
        ofs.write(reinterpret_cast<const char*>(&sig.carry_type), sizeof(sig.carry_type));
        ofs.write(reinterpret_cast<const char*>(&sig.alarm_level), sizeof(sig.alarm_level));
        ofs.write(reinterpret_cast<const char*>(&sig.recent_time), sizeof(sig.recent_time));
        ofs.write(reinterpret_cast<const char*>(&sig.burst_num), sizeof(sig.burst_num));
        ofs.write(reinterpret_cast<const char*>(&sig.avg_duration), sizeof(sig.avg_duration));
        ofs.write(reinterpret_cast<const char*>(&time_count), sizeof(time_count));

        for (const auto& t : sig.times)
        {
            int64_t bgn = t.first;
            int64_t end = t.second;
            ofs.write(reinterpret_cast<const char*>(&bgn), sizeof(bgn));
            ofs.write(reinterpret_cast<const char*>(&end), sizeof(end));
        }
    }

    ofs.close();
    return 0;
}

std::map<int64_t, DetectedSignalRecord> DetectedSignalStore::readFromFile(
    const std::string& filePath, int64_t& out_file_id)
{
    std::map<int64_t, DetectedSignalRecord> signals;
    std::ifstream ifs(filePath, std::ios::binary);
    if (!ifs.is_open()) return signals;

    int32_t signal_num = 0;
    ifs.read(reinterpret_cast<char*>(&out_file_id), sizeof(out_file_id));
    ifs.read(reinterpret_cast<char*>(&signal_num), sizeof(signal_num));

    for (int32_t i = 0; i < signal_num; i++)
    {
        DetectedSignalRecord sig;
        int32_t time_count = 0;

        ifs.read(reinterpret_cast<char*>(&sig.id), sizeof(sig.id));
        ifs.read(reinterpret_cast<char*>(&sig.fc), sizeof(sig.fc));
        ifs.read(reinterpret_cast<char*>(&sig.bw), sizeof(sig.bw));
        ifs.read(reinterpret_cast<char*>(&sig.carry_type), sizeof(sig.carry_type));
        ifs.read(reinterpret_cast<char*>(&sig.alarm_level), sizeof(sig.alarm_level));
        ifs.read(reinterpret_cast<char*>(&sig.recent_time), sizeof(sig.recent_time));
        ifs.read(reinterpret_cast<char*>(&sig.burst_num), sizeof(sig.burst_num));
        ifs.read(reinterpret_cast<char*>(&sig.avg_duration), sizeof(sig.avg_duration));
        ifs.read(reinterpret_cast<char*>(&time_count), sizeof(time_count));

        sig.times.reserve(time_count);
        for (int32_t j = 0; j < time_count; j++)
        {
            int64_t bgn = 0, end = 0;
            ifs.read(reinterpret_cast<char*>(&bgn), sizeof(bgn));
            ifs.read(reinterpret_cast<char*>(&end), sizeof(end));
            sig.times.push_back({ bgn, end });
        }

        signals[sig.id] = std::move(sig);
    }

    ifs.close();
    return signals;
}

// ====== SignalRepoCore ======

SignalRepoCore::SignalRepoCore()
{
}

SignalRepoCore::~SignalRepoCore()
{
    reset();
}

int SignalRepoCore::init(const std::string& spec_data_dir, const std::string& db_path)
{
    reset();
    _spec_data_dir = spec_data_dir;
    _db_path = db_path;

    //创建目录
    if (0 != _access(_spec_data_dir.c_str(), 0))
    {
        if (_mkdir(_spec_data_dir.c_str()) != 0 && errno != EEXIST)
            return -1;
    }

    if (!_db_path.empty())
    {
        // 确保父目录存在（sqlite3_open 不会自动创建目录）
        size_t sep = _db_path.find_last_of("/\\");
        if (sep != std::string::npos)
        {
            std::string dir = _db_path.substr(0, sep);
            if (!dir.empty())
            {
                size_t pos = 0;
                while (true)
                {
                    pos = dir.find_first_of("/\\", pos + 1);
                    if (pos == std::string::npos)
                    {
                        if (_mkdir(dir.c_str()) != 0 && errno != EEXIST)
                            return -1;
                        break;
                    }
                    std::string sub = dir.substr(0, pos);
                    if (!sub.empty())
                    {
                        if (_mkdir(sub.c_str()) != 0 && errno != EEXIST)
                            return -1;
                    }
                }
            }
        }
        if (!_db.Open(_db_path))
        {
            return -1;
        }
        _db.CreateTables();
    }

    _initialized = true;
    return 0;
}

int SignalRepoCore::setSourceDetail(const SourceDetail& source_detail)
{
    _source_detail = source_detail;
    return 0;
}

int SignalRepoCore::writeData(const HQSigMF& sig_mf, SpectrumFileInfoRecord& out_info)
{
    if (!_initialized) return -1;

    // 标记新文件：关闭当前文件，下次 writeData 创建新文件
    if (_need_new_file)
    {
        if (!_current_file_name.empty())
        {
            flushDetsig();
            _detsig_store.clear();
            _current_file_name.clear();
            _current_file_id = 0;
        }
        _need_new_file = false;
    }

    // 首次调用时生成文件名并插入 SpectrumFileInfo 记录
    std::string str_timestamp_ms = utils::timestampMsToCompactString(sig_mf.config().physical_timestamp_ns / NS_PER_MS);
    if (_current_file_name.empty())
    {
        //_current_file_name = generateFileName();
        _current_file_name = str_timestamp_ms + ".dat";

        SpectrumFileInfoRecord info;
        info.file_name      = _current_file_name;
        info.device         = _source_detail.name;
        info.start_freq_mhz = (_source_detail.fc - _source_detail.span / 2) / 1e6;
        info.end_freq_mhz   = (_source_detail.fc + _source_detail.span / 2) / 1e6;
        info.rbw_hz = _source_detail.rbw;
        info.file_size      = 0;
        info.signal_count   = 0;
        info.alarm_count    = 0;
        info.len_per_spec   = 0;
        info.spec_num       = 0;
        info.ref_level = _source_detail.ref_level;

        {
            auto freq_sections = sig_mf.getSections(Domain::FREQUENCY);
            if (!freq_sections.empty() && freq_sections[0]->property.frequency_num > 0)
                info.len_per_spec = freq_sections[0]->property.frequency_num;
        }

        info.start_time_ns = sig_mf.config().physical_timestamp_ns;
        info.end_time_ns = sig_mf.config().physical_timestamp_ns;
        info.start_time = str_timestamp_ms;
        info.end_time   = str_timestamp_ms;

        _current_file_id = insertSpectrumFileInfoRecord(info);
        if (_current_file_id < 0) return -1;
        out_info = info;
    }

    std::string full_path = _spec_data_dir + "/" + _current_file_name;

    // 1. 追加写频谱数据文件
    auto freq_data = const_cast<HQSigMF&>(sig_mf).getData<void>(Domain::FREQUENCY);
    sqlite3_int64 written = 0;
    if (freq_data.first && freq_data.second > 0)
    {
        if (writeFile(full_path, freq_data.first, freq_data.second) != 0)
            return -1;
        written = static_cast<sqlite3_int64>(freq_data.second);
    }

    // 1b. 追加写时间戳文件（与频谱数据文件同名，后缀为.timestamp）
    {
        std::string timestamp_path = _spec_data_dir + "/"
            + _current_file_name.substr(0, _current_file_name.find_last_of('.'))
            + ".timestamp";
        auto& cfg = sig_mf.config();
        writeTimestampFile(timestamp_path, sig_mf.config().physical_timestamp_ns);
    }

    // 2. 提取检测目标写入内存
    {
        auto obj_data = const_cast<HQSigMF&>(sig_mf).getData<HQSigMF::DetectionObject>(Domain::OBJECTS);
        if (obj_data.first && obj_data.second > 0)
        {
            auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();

            for (size_t i = 0; i < obj_data.second; ++i)
            {
                _detsig_store.addOrUpdate(obj_data.first[i], now_ms);
            }
        }
    }

    // 3. 更新 SpectrumFileInfo（文件大小、结束时间、信号数）
    SpectrumFileInfoRecord upd = _db.GetSpectrumFileInfoRecord(_current_file_id);
    upd.file_size += written;
    upd.end_time = str_timestamp_ms;
    upd.end_time_ns = sig_mf.config().physical_timestamp_ns;
    upd.signal_count = _detsig_store.signalCount();
    {
        int alarm_count = 0;
        for (const auto& sig : _detsig_store.getSignals())
            if (sig.second.alarm_level > 0) alarm_count++;
        upd.alarm_count = alarm_count;
    }

    {
        auto freq_sections = sig_mf.getSections(Domain::FREQUENCY);
        if (!freq_sections.empty()) {
            auto& prop = freq_sections[0]->property;
            if (upd.len_per_spec == 0 && prop.frequency_num > 0)
                upd.len_per_spec = prop.frequency_num;
            if (prop.frequency_num > 0 && written > 0) {
                int elem_size = getDataTypeSize(prop.data_type);
                if (elem_size > 0) {
                    int64_t bytes_per_frame = static_cast<int64_t>(prop.frequency_num) * elem_size;
                    if (bytes_per_frame > 0)
                        upd.spec_num += static_cast<int32_t>(written / bytes_per_frame);
                }
            }
        }
    }

    _db.UpdateSpectrumFileInfoRecord(upd);
    out_info = upd;

    return 0;
}

std::map<int64_t, DetectedSignalRecord> SignalRepoCore::readDetsigByFileId(sqlite3_int64 file_id)
{
    if (!_initialized) return {};

    // 如果是当前正在录制的文件，返回内存中的数据
    if (file_id == _current_file_id)
    {
        return _detsig_store.getSignals();
    }

    // 否则从 .detsig 文件读取
    SpectrumFileInfoRecord info = _db.GetSpectrumFileInfoRecord(file_id);
    if (info.id == 0) return {};

    std::string detsig_path = getDetsigFilePath(info.file_name);
    int64_t stored_file_id = 0;
    return DetectedSignalStore::readFromFile(detsig_path, stored_file_id);
}

std::vector<SpectrumFileInfoRecord> SignalRepoCore::getAllFileInfos()
{
    if (!_initialized) return {};
    return _db.GetAllSpectrumFileInfoRecords();
}

int SignalRepoCore::deleteFiles(const std::vector<sqlite3_int64>& file_ids)
{
    if (!_initialized) return -1;
    for (auto file_id : file_ids)
    {
        SpectrumFileInfoRecord rec = _db.GetSpectrumFileInfoRecord(file_id);
        if (rec.id == 0) continue;

        // 删除 .dat 文件
        std::string dat_path = _spec_data_dir + "/" + rec.file_name;
        DeleteFileA(dat_path.c_str());

        // 删除 .timestamp 文件
        std::string ts_path = _spec_data_dir + "/"
            + rec.file_name.substr(0, rec.file_name.find_last_of('.'))
            + ".timestamp";
        DeleteFileA(ts_path.c_str());

        // 删除 .detsig 文件
        std::string detsig_path = getDetsigFilePath(rec.file_name);
        DeleteFileA(detsig_path.c_str());

        // 删除数据库记录
        _db.DeleteSpectrumFileInfoRecord(file_id);
    }
    return 0;
}

int SignalRepoCore::deleteSignals(sqlite3_int64 file_id, const std::vector<sqlite3_int64>& signal_ids)
{
    if (!_initialized || signal_ids.empty()) return -1;

    // 读取当前文件的所有信号
    auto signals = readDetsigByFileId(file_id);
    if (signals.empty()) return -1;

    // 移除指定 ID 的信号
    for (auto id : signal_ids)
        signals.erase(id);

    // 更新数据库中的 signal_count 和 alarm_count
    SpectrumFileInfoRecord upd = _db.GetSpectrumFileInfoRecord(file_id);
    if (upd.id == 0) return -1;

    upd.signal_count = static_cast<int>(signals.size());
    {
        int alarm_count = 0;
        for (const auto& sig : signals)
            if (sig.second.alarm_level > 0) alarm_count++;
        upd.alarm_count = alarm_count;
    }
    _db.UpdateSpectrumFileInfoRecord(upd);

    // 如果是当前正在录制的文件，同步更新内存中的 store
    if (file_id == _current_file_id)
    {
        auto& mutable_signals = const_cast<std::map<int64_t, DetectedSignalRecord>&>(
            _detsig_store.getSignals());
        for (auto id : signal_ids)
            mutable_signals.erase(id);
    }

    // 重写 .detsig 文件
    std::string detsig_path;
    if (file_id == _current_file_id && !_current_file_name.empty())
        detsig_path = getDetsigFilePath(_current_file_name);
    else
    {
        SpectrumFileInfoRecord info = _db.GetSpectrumFileInfoRecord(file_id);
        if (info.id == 0) return -1;
        detsig_path = getDetsigFilePath(info.file_name);
    }

    return DetectedSignalStore::writeToFile(detsig_path, signals, file_id);
}

int SignalRepoCore::flushDetsig()
{
    if (_current_file_name.empty() || _current_file_id == 0)
        return 0;

    std::string detsig_path = getDetsigFilePath(_current_file_name);
    return DetectedSignalStore::writeToFile(detsig_path,
        _detsig_store.getSignals(), _current_file_id);
}

void SignalRepoCore::markNewFile()
{
    _need_new_file = true;
}

// ====== protected ======

void SignalRepoCore::reset()
{
    flushDetsig();
    _detsig_store.clear();

    if (_db.IsOpen())
    {
        _db.Close();
    }
    _spec_data_dir.clear();
    _db_path.clear();
    _source_detail = SourceDetail();
    _current_file_name.clear();
    _current_file_id = 0;
    _initialized = false;
}

int SignalRepoCore::writeFile(const std::string& filePath, const void* data, size_t size)
{
    if (!data || size == 0) return -1;

    std::ofstream ofs(filePath, std::ios::binary | std::ios::app);
    if (!ofs.is_open()) return -1;

    ofs.write(static_cast<const char*>(data), size);
    if (!ofs.good()) return -1;

    ofs.close();
    return 0;
}

int SignalRepoCore::writeTimestampFile(const std::string& filePath, int64_t timestamp_ns)
{
    std::ofstream ofs(filePath, std::ios::app);
    if (!ofs.is_open()) return -1;
    ofs << timestamp_ns << "\n";
    ofs.close();
    return 0;
}

sqlite3_int64 SignalRepoCore::insertSpectrumFileInfoRecord(SpectrumFileInfoRecord& info)
{
    if (!_db.InsertSpectrumFileInfoRecord(info))
    {
        return -1;
    }
    return info.id;
}

std::string SignalRepoCore::generateFileName()
{
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    localtime_s(&tm, &t);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d_%H%M%S")
        << "_" << std::setfill('0') << std::setw(3) << ms.count()
        << ".dat";
    return oss.str();
}

std::string SignalRepoCore::getDetsigFilePath(const std::string& datFileName) const
{
    return _spec_data_dir + "/"
        + datFileName.substr(0, datFileName.find_last_of('.'))
        + ".detsig";
}
