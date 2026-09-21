#pragma once
#include <string>
#include <fstream>
#include <ctime>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <vector>
#include <unordered_map>
#include "sqlite/sqlite_wrapper.hpp"
#include "radioai/icd/HQSigMF.hpp"
#include "radioai/icd/SignalData.hpp"

struct DetectedSignalRecord {
    int64_t id;
    int64_t fc;
    int32_t bw;
    int32_t carry_type;
    int32_t alarm_level;
    int64_t recent_time;
    int32_t burst_num;
    int64_t avg_duration;
    int64_t sum_duration;
    std::vector<std::pair<int64_t, int64_t>> times;
};

class DetectedSignalStore {
public:
    void addOrUpdate(const HQSigMF::DetectionObject& obj, int64_t timestamp_ms);
    const std::map<int64_t, DetectedSignalRecord>& getSignals() const { return _signals; }
    int signalCount() const { return static_cast<int>(_signals.size()); }
    void clear();

    static int writeToFile(const std::string& filePath,
        const std::map<int64_t, DetectedSignalRecord>& signals, int64_t file_id);
    static std::map<int64_t, DetectedSignalRecord> readFromFile(
        const std::string& filePath, int64_t& out_file_id);

private:
    std::map<int64_t, DetectedSignalRecord> _signals;
    std::unordered_map<int64_t, size_t> _fc_to_idx;
    int64_t _next_id = 1;
};

class SignalRepoCore
{
public:
    SignalRepoCore();
    ~SignalRepoCore();

    int  init(const std::string& spec_data_dir, const std::string& db_path);

    int setSourceDetail(const SourceDetail& source_detail);

    int writeData(const HQSigMF& sig_mf, SpectrumFileInfoRecord& out_info);

    std::map<int64_t, DetectedSignalRecord> readDetsigByFileId(sqlite3_int64 file_id);

    std::vector<SpectrumFileInfoRecord> getAllFileInfos();
    int deleteFiles(const std::vector<sqlite3_int64>& file_ids);
    int deleteSignals(sqlite3_int64 file_id, const std::vector<sqlite3_int64>& signal_ids);

    int flushDetsig();
    /** @brief 标记下次 writeData 时关闭当前文件并创建新文件（线程安全，仅设标志） */
    void markNewFile();

protected:
    void reset();

    int  writeFile(const std::string& filePath, const void* data, size_t size);
    int  writeTimestampFile(const std::string& filePath, int64_t timestamp_ns);

    sqlite3_int64 insertSpectrumFileInfoRecord(SpectrumFileInfoRecord& info);

    std::string generateFileName();
    std::string getDetsigFilePath(const std::string& datFileName) const;

private:
    std::string   _spec_data_dir;
    std::string   _db_path;
    SourceDetail  _source_detail;
    SQLiteDB      _db{};
    bool          _initialized = false;

    std::string   _current_file_name;
    sqlite3_int64 _current_file_id = 0;

    DetectedSignalStore _detsig_store;
    bool _need_new_file = false;
};
