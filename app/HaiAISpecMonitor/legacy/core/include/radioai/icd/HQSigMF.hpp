#pragma once

#include <iostream>
#include <cstdint>
#include <string>
#include <complex>
#include <vector>
#include <memory>
#include <cmath>
#include <random>
#include <functional>
#include <type_traits>
#include <map>
#include <cstring>
#include "radioai/ComData.hpp"

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// 1 毫秒对应的纳秒数，供时间单位换算时保持统一。
#ifndef NS_PER_MS
#define NS_PER_MS (1000000)
#endif

/** HQSigMF 中一组数据段所属的业务域。 */
enum class Domain : int32_t { TIME, FREQUENCY, TIME_FREQUENCY, OBJECTS, FEATURE, UNKNOWN };
/** 数据段中每个元素的标量或复数存储类型。 */
enum class DataType : int32_t { S8, S16, S32, F32, F64, SC8, SC16, SC32, FC32, FC64, kVOID, OBJ};
/** 识别结果中常见的调制方式枚举。 */
enum class ModulationType : int32_t { AM, FM, FSK, GFSK, BPSK, QPSK, QAM16, QAM64, OFDM, LORA, CW, UNKNOWN };

// 域内的二级类别由生产者和消费者约定；数值会随序列化数据一起传递。
enum class TimeCategory : int32_t {RAW};
enum class FrequencyCategory : int32_t {SHORT_TERM,LONG_TERM};    //短时平均谱、长时统计谱
enum class FeatureCategory : int32_t {
    DEAULT = 0,
    DETECTION_PROGRESS = 1,
    TRACKING_RESULT = 2,
    WHITELIST_MATCH = 3
};
enum class ObjCategory : int32_t { DEAULT };

/** 大带宽检测器随数据包发布的处理阶段。 */
enum class DetectionStage : int32_t {
    WARMING_UP = 0,
    PROVISIONAL = 1,
    READY = 2,
    ERROR_STATE = 3
};

/** 跟踪器对检测对象给出的生命周期状态。 */
enum class TrackLifeState : int32_t {
    TENTATIVE = 0,
    CONFIRMED = 1,
    COASTING = 2,
    EXPIRED = 3
};

inline int getDataTypeSize(DataType dt)
{
    // 返回单个元素占用的字节数。复数类型按两个同宽实部/虚部计算。
    int chann_num = 1; 
    if(dt > DataType::F64)
    {
        chann_num = 2;
        dt = static_cast<DataType>(static_cast<int>(dt) - static_cast<int>(DataType::SC8));
    }
    int result = (dt == DataType::S8)  ? 1 :
                (dt == DataType::S16)  ? 2 :
                (dt == DataType::SC16) ? 4 :
                (dt == DataType::S32)  ? 4 :
                (dt == DataType::F32)  ? 4 :
                (dt == DataType::FC32) ? 8 : 0;
    return result * chann_num; 
}

inline bool isComplex(DataType dt){
    // kVOID 没有元素类型；其余复数枚举位于实数枚举之后。
    if (DataType::kVOID == dt) { return false; }
    return dt > DataType::F64? true : false; 
}

// 将 C++ 模板类型映射为协议中的 DataType，未知类型统一作为对象处理。
template<typename T>
inline DataType getTypeId() {
    if (std::is_same<T, int8_t>::value) return DataType::S8;
    if (std::is_same<T, int16_t>::value) return DataType::S16;
    if (std::is_same<T, int32_t>::value) return DataType::S32;
    if (std::is_same<T, float>::value) return DataType::F32;
    if (std::is_same<T, double>::value) return DataType::F64;
    if (std::is_same<T, std::complex<int8_t>>::value) return DataType::SC8;
    if (std::is_same<T, std::complex<int16_t>>::value) return DataType::SC16;
    if (std::is_same<T, std::complex<int32_t>>::value) return DataType::SC32;
    if (std::is_same<T, std::complex<float>>::value) return DataType::FC32;
    if (std::is_same<T, std::complex<double>>::value) return DataType::FC64;
    if (std::is_same<T, void>::value) return DataType::kVOID;
    return DataType::OBJ;
}

/**
 * @brief 面向信号处理链路的元数据与数据段容器。
 *
 * 一个 HQSigMF 对象保存全局采样信息，并按 Domain 将多个 Section 分组。
 * Section 负责数据所有权和类型描述，容器本身通过 ComData 接口在组件间传递。
 */
class HQSigMF : public radioai::core::ComData
{
public:
    struct GlobalConfig
    {
        // 全局采样上下文：频率和带宽单位为 Hz，sample_start 为样本序号。
        std::string version = "1.0.0";
        std::string author = "hq";
        std::string hw = "Generic SDR";
        int64_t center_frequency = 0;
        int64_t sample_rate = 0;
        int64_t bandwidth = 0;
        int64_t sample_start = 0;
        int64_t physical_timestamp_ns = 0;           // 自 1970-01-01 UTC 起的物理时间戳，单位 ns。
        GlobalConfig& setVersion(const std::string& ver) { version = ver; return *this; }
        GlobalConfig& setAuthor(const std::string& auth) { author = auth; return *this; }
        GlobalConfig& setHardware(const std::string& hardware) { hw = hardware; return *this; }
        GlobalConfig& setCenterFreq(int64_t freq) { center_frequency = freq; return *this; }
        GlobalConfig& setSampleRate(int64_t rate) { sample_rate = rate; return *this; }
        GlobalConfig& setBandwidth(int64_t bw) { bandwidth = bw; return *this; }
        GlobalConfig& setSampleStart(int64_t start) { sample_start = start; return *this; }
        GlobalConfig& setPhysicalTimestampNs(int64_t ts_ns) { physical_timestamp_ns = ts_ns; return *this; }
        // 根据样本序号换算相对时间；调用方需先保证 sample_rate 非零。
        int64_t getTimestampNs() { return 1ll * (1e9 * sample_start / sample_rate); }
    };

    struct SectionProperty
    {
        // 描述一段数据的域、二级类别、元素类型和时间/频率维度。
        Domain domain = Domain::TIME;          //域类型 
        int32_t category = 0;                  //种类，二级分类
        DataType data_type = DataType::SC16;
        int32_t frequency_num;
        float time_span_ms;
        SectionProperty& setDomain(Domain d) { domain = d; return *this; }
        SectionProperty& setCategory(int32_t cate) { category = cate; return *this; }
        SectionProperty& setDataType(DataType dt) { data_type = dt; return *this; }
        SectionProperty& setFrequencyNum(int32_t num) { frequency_num = num; return *this; }
        SectionProperty& setTimeSpan(float span) { time_span_ms = span; return *this; }
    };

    // 检测对象的时间、频率、质量、分类和告警属性；数组字段按 C 字符串使用。
    struct DetectionObject
    {
        // 这些基础数值字段没有统一默认值，创建对象后应通过 setter 或聚合初始化完整赋值。
        int64_t id;
        float t_start_ms;
        float t_end_ms;
        int64_t t_start_ns;
        int64_t t_end_ns;
        int64_t f_start_hz;
        int64_t f_end_hz;

        float snr;                               //信噪比，单位dB
        float symbol_rate;                       //符号速率，单位kBd

        int8_t carry_type;                       //载波类型 0 - 常在 1 - 突发
        int8_t alarm_level;                      //告警等级 0-正常、1-一般、2-严重

        // 信号种类
        int8_t signal_class[64] = "UNKNOWN";
        float signal_class_confidence = 0.0f;

        int8_t emitter_class[64] = "UNKNOWN";
        float emitter_class_confidence = 0.0f;

        int8_t modulation[64] = "UNKNOWN";
        float mod_confidence;
        DetectionObject& setId(int64_t obj_id) { id = obj_id; return *this; }
        DetectionObject& setStartMs(int64_t start_ms) { t_start_ms = start_ms; return *this; }
        DetectionObject& setEndMs(int64_t end_ms) { t_end_ms = end_ms; return *this; }
        DetectionObject& setStartNs(int64_t start_ns) { t_start_ns = start_ns; return *this; }
        DetectionObject& setEndNs(int64_t end_ns) { t_end_ns = end_ns; return *this; }
        DetectionObject& setFreqStartHz(int64_t start_hz) { f_start_hz = start_hz; return *this; }
        DetectionObject& setFreqEndHz(int64_t end_hz) { f_end_hz = end_hz; return *this; }
        DetectionObject& setSNR(float s) { snr = s; return *this; }
        DetectionObject& setSymbolRate(float sr) { symbol_rate = sr; return *this; }
        DetectionObject& setSignalClass(const std::string& prot) {
            memset(signal_class, 0, 64);
            int cp_size = prot.size();
            cp_size = cp_size >= 64 ? 63 : cp_size;
            memcpy(signal_class, prot.c_str(), cp_size);
            ; return *this; }
        DetectionObject& setEmitterClass(const std::string& prot) {
            memset(emitter_class, 0, 64);
            int cp_size = prot.size();
            cp_size = cp_size >= 64 ? 63 : cp_size;
            memcpy(emitter_class, prot.c_str(), cp_size);
            ; return *this;
        }

        DetectionObject& setSignalClassConfidence(float prot_confidence) { signal_class_confidence = prot_confidence; return *this; }
        DetectionObject& setEmitterClassConfidence(float prot_confidence)  { emitter_class_confidence = prot_confidence; return *this; }
        DetectionObject& setModulationType(const std::string & prot) {
            memset(modulation, 0, 64);
            int cp_size = prot.size();
            cp_size = cp_size >= 64 ? 63 : cp_size;
            memcpy(modulation, prot.c_str(), cp_size);
            ; return *this;
        }
        DetectionObject& setModConfidence(float modulation_confidence) { mod_confidence = modulation_confidence; return *this; }
    };

    /**
     * @brief 大带宽检测进度。
     *
     * 该结构按原始字节存入 FEATURE/DETECTION_PROGRESS/OBJ Section，
     * 因此只包含固定宽度的平凡可复制字段。
     */
    struct DetectionProgress
    {
        DetectionStage stage;
        int32_t accumulated_frames;
        int32_t required_frames;
        int32_t min_output_frames;
    };
    static_assert(std::is_standard_layout<DetectionProgress>::value &&
                  std::is_trivially_copyable<DetectionProgress>::value,
                  "DetectionProgress must remain a POD payload");

    /**
     * @brief 单个跟踪目标的原始量测、平滑结果和生命周期信息。
     *
     * object_index >= 0 时指向同一数据包 OBJECTS 中的对象；负值表示没有当前
     * 量测的纯状态事件（例如 COASTING/EXPIRED）。version 和 record_size 用于
     * 后续兼容扩展。
     */
    struct TrackingResult
    {
        int32_t version;
        int32_t record_size;
        int32_t object_index;
        TrackLifeState state;
        int64_t track_id;
        int64_t raw_start_hz;
        int64_t raw_end_hz;
        int64_t tracked_start_hz;
        int64_t tracked_end_hz;
        int64_t last_seen_ns;
        float match_score;
        int32_t hit_count;
        int32_t miss_count;
    };
    static_assert(std::is_standard_layout<TrackingResult>::value &&
                  std::is_trivially_copyable<TrackingResult>::value,
                  "TrackingResult must remain a POD payload");

    /** 白名单匹配元数据；白名单不再改写 DetectionObject 的实时频率边界。 */
    struct WhitelistMatch
    {
        int32_t version;
        int32_t record_size;
        int32_t object_index;
        int32_t matched;
        int64_t track_id;
        int64_t whitelist_id;
        int64_t canonical_start_hz;
        int64_t canonical_end_hz;
    };
    static_assert(std::is_standard_layout<WhitelistMatch>::value &&
                  std::is_trivially_copyable<WhitelistMatch>::value,
                  "WhitelistMatch must remain a POD payload");

    // 段数据只保存地址和字节数；实际释放由 Section 的所有权策略负责。
    struct SectionData{
        int32_t size = 0;              //数据大小
        void* data = nullptr;          //数据指针 
    };

    // 一段可独立访问的数据及其属性。默认构造路径复制到 malloc 内存，池化路径持有 shared_ptr。
    struct Section
    {
        /*成员变量*/
        SectionProperty property;   //属性
        SectionData data;           //数据

        /*
        * @brief：段构造函数
        * @param prop：段属性
        * @param size：段数据字节大小
        * @param in_data：填充数据指针，若nullptr则不填充。
        */
        Section(const SectionProperty& prop, int32_t size, const void* in_data) : property(prop)
        {
            if (0 < size) {
                data.data = malloc(size);
                data.size = size;
                if (nullptr == data.data) throw std::runtime_error("Construction failed");
                if (nullptr != in_data) {
                    memcpy(data.data, in_data, size);
                }
            }
        }

        /*
        * @brief：段构造函数（池化内存，析构时自动归还池）
        * @param prop：段属性
        * @param buf：BufferList 分配的 shared_ptr
        * @param size：数据字节大小
        */
        Section(const SectionProperty& prop, std::shared_ptr<void> buf, int32_t size)
            : property(prop), _data_owner(std::move(buf))
        {
            data.data = _data_owner.get();
            data.size = size;
        }

        virtual ~Section(){
            // 池化内存只释放持有权；普通内存由本对象释放，避免重复释放。
            if (_data_owner) {
                data.data = nullptr;
                data.size = 0;
            } else if (nullptr != data.data) {
                free(data.data);
                data.data = nullptr;
            }
        }

        /*
        * @brief：获取数据
        * @return：key：数据单元指针 value：数据单元个数
        *    特例：key为void，value为数据实际字节大小
        */
        template <typename T>
        typename std::enable_if<!std::is_void<T>::value,
            std::pair<const T*, size_t>>::type
        getData()
        {
            // 类型不匹配时返回空结果，避免把错误字节解释为调用方请求的类型。
            if (property.data_type != getTypeId<T>()) {
                return { nullptr, 0 };
            }

            return {
                reinterpret_cast<const T*>(data.data),
                data.size / sizeof(T)
            };
        }

        // void 版本返回原始地址和字节数，用于对象或未预先声明元素类型的数据。
        template <typename T>
        typename std::enable_if<std::is_void<T>::value,
            std::pair<const void*, size_t>>::type
            getData()
        {
            return {
                reinterpret_cast<const void*>(data.data),
                data.size
            };
        }

        /* 获取单个元素大小；未知类型返回 0。 */
        int getItemSize(){return getDataTypeSize(property.data_type);}

        /* 获取段声明的元素类型。 */
        DataType getItemType(){return property.data_type;}

    private:
        std::shared_ptr<void> _data_owner;  // 池化内存持有权
    };
public:
    // 默认构造后仍可通过 config() 设置采样上下文，再追加数据段。
    HQSigMF() = default;
    virtual ~HQSigMF() = default;
    HQSigMF(const GlobalConfig& global) : global(global) {}

    // 工厂方法返回一个空容器；CreateWithGlobalConfig 同时初始化全局采样参数。
    static HQSigMF CreateDefault() { return HQSigMF(); }
    static HQSigMF CreateWithGlobalConfig(
        std::string version = "1.0.0",
        std::string author = "hq",
        std::string hw = "Generic SDR",
        int64_t center_frequency = 1000,
        int64_t sample_rate = 48000,
        int64_t bw = 48000,
        int64_t sample_start = 0.0f)
    {
        HQSigMF signal;
        //signal.global = { version,author,hw ,center_frequency,sample_rate,sample_start };
        signal.global.setVersion(version);
        signal.global.setAuthor(author);
        signal.global.setHardware(hw);
        signal.global.setCenterFreq(center_frequency);
        signal.global.setSampleRate(sample_rate);
        signal.global.setBandwidth(bw);
        signal.global.setSampleStart(sample_start);
        return signal;
    }

    HQSigMF& createWithGlobalConfig(
        std::string version = "1.0.0",
        std::string author = "hq",
        std::string hw = "Generic SDR",
        int64_t center_frequency = 1000,
        int64_t sample_rate = 48000,
        int64_t bw = 48000,
        int64_t sample_start = 0.0f)
    {
        global.version = version;
        global.author = author;
        global.hw = hw;
        global.center_frequency = center_frequency;
        global.sample_rate = sample_rate;
        global.bandwidth = bw;
        global.sample_start = sample_start;
        return *this;
    }

    virtual radioai::core::ComData* clone() override
    {
        auto clone_obj = new (std::nothrow) HQSigMF;
        if (nullptr == clone_obj)return nullptr;
        *clone_obj = *this;
        return clone_obj;
    }

    // 生命周期令牌用于让外部缓冲区在数据包经过多级组件时保持存活。
    void setKeepAlive(std::shared_ptr<void> sp) { _keep_alive = std::move(sp); }
    std::shared_ptr<void> getKeepAlive() const { return _keep_alive; }
    std::shared_ptr<void> releaseKeepAlive() { return std::move(_keep_alive); }

    int32_t addSection(const std::shared_ptr<Section>& section)
    {
        // 共享指针直接进入按域索引的列表，容器不复制 Section 数据。
        if (nullptr == section)return -1;
        sections[section->property.domain].push_back(section);
        return 0;
    }

    template <typename T1, typename T2 = Section>
    int32_t addSection(const SectionProperty& sec_prop, int num, const T1* data)
    {
        // 按元素个数计算字节数，并由 Section 复制输入数据以获得独立所有权。
        auto section = std::make_shared<T2>(sec_prop, num * sizeof(T1), (const void*)data);
        if (nullptr == section)return -1;
        sections[sec_prop.domain].push_back(section);
        return 0;
    }

    template <typename T1, typename T2 = Section>
    int32_t addTimeDomainSection(int num, const T1* data, int category = static_cast<int32_t>(TimeCategory::RAW))
    {
        // 原始时域数据的 time_span_ms 由样本数和全局采样率推导。
        SectionProperty sec_prop;
        sec_prop.domain = Domain::TIME;
        sec_prop.category = category;
        sec_prop.data_type = getTypeId<T1>();
        sec_prop.time_span_ms = num / static_cast<float>(global.sample_rate) * 1000.0f; // 转换为毫秒

        return addSection<T1, T2>(sec_prop, num, data);
    }

    template <typename T1, typename T2 = Section>
    int32_t addFrequencyDomainSection(int frequency_num, float time_span, int num, const T1* data, int category = static_cast<int32_t>(FrequencyCategory::SHORT_TERM))
    {
        // 频域段额外记录频率 bin 数和覆盖时长，便于下游还原频谱坐标。
        SectionProperty sec_prop;
        sec_prop.domain = Domain::FREQUENCY;
        sec_prop.category = category;
        sec_prop.data_type = getTypeId<T1>();
        sec_prop.frequency_num = frequency_num;
        sec_prop.time_span_ms = time_span;

        return addSection<T1, T2>(sec_prop, num, data);
    }

    template <typename T1, typename T2 = Section>
    int32_t addFeatureDomainSection(int num, const T1* data, int category = static_cast<int32_t>(FeatureCategory::DEAULT))
    {
        // 特征段沿用样本时长字段；具体特征含义由 category 约定。
        SectionProperty sec_prop;
        sec_prop.domain = Domain::FEATURE;
        sec_prop.category = category;
        sec_prop.data_type = getTypeId<T1>();
        sec_prop.time_span_ms = num / static_cast<float>(global.sample_rate) * 1000.0f; // 转换为毫秒

        return addSection<T1, T2>(sec_prop, num, data);
    }

    int32_t addObjectsSection(int num, const DetectionObject* data, int category = static_cast<int32_t>(ObjCategory::DEAULT))
    {
        // 检测对象作为 POD 数组复制保存，DataType 固定为 OBJ。
        SectionProperty sec_prop;
        sec_prop.domain = Domain::OBJECTS;
        sec_prop.category = category;
        sec_prop.data_type = DataType::OBJ;

        auto section = std::make_shared<Section>(sec_prop, num * sizeof(DetectionObject), (const void*)data);
        if (nullptr == section)return -1;
        sections[sec_prop.domain].push_back(section);
        return 0;
    }

    // 删除一个域下的全部 Section；共享指针引用归零后自动回收数据。
    HQSigMF& rmvSections(Domain domain)
    {
        sections.erase(domain);
        return *this;
    }

    // 删除指定域和类别的 Section，保留该域中的其他类别。
    HQSigMF& rmvSection(Domain domain, int32_t category)
    {
        auto it = sections.find(domain);
        if (sections.end() == it) return *this;

        auto& domain_sections = it->second;
        for (auto section_it = domain_sections.begin(); section_it != domain_sections.end();)
        {
            if (*section_it && (*section_it)->property.category == category)
                section_it = domain_sections.erase(section_it);
            else
                ++section_it;
        }
        if (domain_sections.empty()) sections.erase(it);
        return *this;
    }

    // 返回指定域的 Section 列表副本；列表元素仍与容器共享所有权。
    std::vector< std::shared_ptr<Section> > getSections(Domain domain)
    {
        std::vector< std::shared_ptr<Section> > ret;
        auto it = sections.find(domain);
        if(sections.end() == it){
            return ret;
        }else{
            return it->second;
        }
    }

    // const 重载仅限制容器本身，不改变返回的共享指针语义。
    std::vector< std::shared_ptr<Section> > getSections(Domain domain) const
    {
        std::vector< std::shared_ptr<Section> > ret;
        auto it = sections.find(domain);
        if(sections.end() == it){
            return ret;
        }else{
            return it->second;
        }
    }

    std::shared_ptr<Section> getSection(Domain domain, int category = 0)
    {
        auto it = sections.find(domain);
        if (sections.end() == it || it->second.empty()) {
            return nullptr;
        }
        else {

            for(auto item : it->second)
            {
                if (item->property.category == category) { return item; }
            }
            return nullptr;
        }
    }

    std::shared_ptr<Section> getSection(Domain domain, int category = 0) const
    {
        auto it = sections.find(domain);
        if (sections.end() == it || it->second.empty()) return nullptr;
        for (const auto& item : it->second)
        {
            if (item && item->property.category == category) return item;
        }
        return nullptr;
    }

    HQSigMF& clearSections()
    {
        sections.clear();
        return *this;
    }

    // 读取指定域第一个段的数据；没有段或类型不匹配时返回空指针和 0。
    template <typename T>
    std::pair<const T*, size_t> getData(Domain domain)
    {
        auto sections = getSections(domain);
        if(sections.empty()){
            return { nullptr, 0 };
        }
        else
        {
            return sections[0]->getData<T>();
        }
    }

    GlobalConfig& config() { return global; }
    const GlobalConfig& config() const { return global; }

    /**
    * @brief 将全局配置和各域 Section 按当前二进制布局写入连续缓冲区。
    * @param[out] data: 序列化后的数据 !!!需要用户手动释放!!!
    * @param[out] len: 序列化后的字符串的大小
    *
    * @return 无
    */
    void serialization(char** data, size_t& len)
    {
        // 输出缓冲区由 new[] 分配，调用方必须使用 delete[] 释放；格式依赖字段顺序。
        // 计算总大小 【4(存储内容大小的int) + 内容大小】
        len = 0;
        len += 4 + global.version.size();
        len += 4 + global.author.size();
        len += 4 + global.hw.size();
        len += sizeof(int32_t);
        len += sizeof(int64_t) * 4;// center_frequency + sample_rate + bandwidth + sample_start

        len += 4 + sizeof(int32_t);// 4 + sectionsSize
        for (auto it = sections.begin(); it != sections.end(); it++)
        {
            std::vector<std::shared_ptr<HQSigMF::Section>> sectionsVec = it->second;
            len += 4 + sizeof(int32_t);// 4 + sectionsVecSize
            for (size_t j = 0; j < sectionsVec.size(); j++)
            {
                len += 4 + sizeof(Domain);
                len += 4 + sizeof(int32_t);
                len += 4 + sizeof(DataType);
                len += 4 + sizeof(int32_t);
                len += 4 + sizeof(float);

                len += 4 + sizeof(int32_t);
                len += sectionsVec[j]->data.size;
            }
        }

        *data = new char[len];
        int pos = 0;


#define SERIALIZATION_DATA(src, len) memcpy(*data + pos, src, len); pos += len;
        // 序列化GlobalConfig
        int versionLen = global.version.size();
        int authorLen = global.author.size();
        int hwLen = global.hw.size();
        SERIALIZATION_DATA(&versionLen, 4)
            SERIALIZATION_DATA(global.version.c_str(), versionLen)
            SERIALIZATION_DATA(&authorLen, 4)
            SERIALIZATION_DATA(global.author.c_str(), authorLen)
            SERIALIZATION_DATA(&hwLen, 4)
            SERIALIZATION_DATA(global.hw.c_str(), hwLen)
            SERIALIZATION_DATA(&global.center_frequency, sizeof(int64_t))
            SERIALIZATION_DATA(&global.sample_rate, sizeof(int64_t))
            SERIALIZATION_DATA(&global.bandwidth, sizeof(int64_t))
            SERIALIZATION_DATA(&global.sample_start, sizeof(int64_t))

            // 序列化数据段
            int sectionsSizeLen = sizeof(int32_t);
        int32_t sectionsSize = sections.size();
        SERIALIZATION_DATA(&sectionsSizeLen, 4)
            SERIALIZATION_DATA(&sectionsSize, sectionsSizeLen)
            for (auto it = sections.begin(); it != sections.end(); it++)
            {
                std::vector<std::shared_ptr<HQSigMF::Section>> sectionVec = it->second;
                int sectionSizeLen = sizeof(int32_t);
                int32_t sectionSize = sectionVec.size();
                SERIALIZATION_DATA(&sectionSizeLen, 4)
                    SERIALIZATION_DATA(&sectionSize, sectionSizeLen)
                    for (size_t j = 0; j < sectionVec.size(); j++)
                    {
                        int domainLen = sizeof(Domain);
                        int category = sizeof(int32_t);
                        int dataTypeLen = sizeof(DataType);
                        int freqLen = sizeof(int32_t);
                        int timeSpanLen = sizeof(float);
                        SERIALIZATION_DATA(&domainLen, 4)
                            SERIALIZATION_DATA(&sectionVec[j]->property.domain, domainLen)
                            SERIALIZATION_DATA(&category, 4)
                            SERIALIZATION_DATA(&sectionVec[j]->property.category, category)
                            SERIALIZATION_DATA(&dataTypeLen, 4)
                            SERIALIZATION_DATA(&sectionVec[j]->property.data_type, dataTypeLen)
                            SERIALIZATION_DATA(&freqLen, 4)
                            SERIALIZATION_DATA(&sectionVec[j]->property.frequency_num, freqLen)
                            SERIALIZATION_DATA(&timeSpanLen, 4)
                            SERIALIZATION_DATA(&sectionVec[j]->property.time_span_ms, timeSpanLen)

                            int numLen = sizeof(int32_t);
                        SERIALIZATION_DATA(&numLen, 4)
                            SERIALIZATION_DATA(&sectionVec[j]->data.size, numLen)
                            SERIALIZATION_DATA(sectionVec[j]->data.data, sectionVec[j]->data.size)
                    }
            }


#undef SERIALIZATION_DATA
    }

    /**
     * @brief 按 serialization 的字段顺序重建全局配置和 Section 列表。
     * @param[in] data: 数据
     * @param[in] len: 数据大小
     *
     * @return 无
    */
    void deserialization(char* data, size_t len)
    {
        // 输入必须来自兼容版本的 serialization；当前实现按信任长度读取，不负责外部校验。
#define DESERIALIZATION_DATA(dst, len) memcpy(dst, data + pos, len); pos += len;
        // 反序列化GlobalConfig
        int pos = 0;
        int versionLen = 0;
        int authorLen = 0;
        int hwLen = 0;
        char* version = NULL;
        char* author = NULL;
        char* hw = NULL;

        DESERIALIZATION_DATA(&versionLen, 4)
            version = new char[versionLen];
        DESERIALIZATION_DATA(version, versionLen)
            DESERIALIZATION_DATA(&authorLen, 4)
            author = new char[authorLen];
        DESERIALIZATION_DATA(author, authorLen)
            DESERIALIZATION_DATA(&hwLen, 4)
            hw = new char[hwLen];
        DESERIALIZATION_DATA(hw, hwLen)
            global.version = std::string(version, versionLen);
        global.author = std::string(author, authorLen);
        global.hw = std::string(hw, hwLen);
        delete[] version;
        delete[] author;
        delete[] hw;
        DESERIALIZATION_DATA(&global.center_frequency, sizeof(int64_t))
            DESERIALIZATION_DATA(&global.sample_rate, sizeof(int64_t))
            DESERIALIZATION_DATA(&global.bandwidth, sizeof(int64_t))
            DESERIALIZATION_DATA(&global.sample_start, sizeof(int64_t))
            // 反序列化数据段
            int sectionsSizeLen = 0;
        int32_t sectionsSize = 0;
        DESERIALIZATION_DATA(&sectionsSizeLen, 4)
            DESERIALIZATION_DATA(&sectionsSize, sectionsSizeLen)
            for (size_t i = 0; i < sectionsSize; i++)
            {
                int sectionSizeLen = 0;
                int32_t sectionSize = 0;
                DESERIALIZATION_DATA(&sectionSizeLen, 4)
                    DESERIALIZATION_DATA(&sectionSize, sectionSizeLen)
                    for (size_t j = 0; j < sectionSize; j++)
                    {
                        int domainLen = 0;
                        int categoryLen = 0;
                        int dataTypeLen = 0;
                        int freqLen = 0;
                        int timeSpanLen = 0;
                        Domain domain;
                        int32_t category;
                        DataType dataType;
                        int32_t freq;
                        float timeSpan;
                        DESERIALIZATION_DATA(&domainLen, 4)
                            DESERIALIZATION_DATA(&domain, domainLen)
                            DESERIALIZATION_DATA(&categoryLen, 4)
                            DESERIALIZATION_DATA(&category, categoryLen)
                            DESERIALIZATION_DATA(&dataTypeLen, 4)
                            DESERIALIZATION_DATA(&dataType, dataTypeLen)
                            DESERIALIZATION_DATA(&freqLen, 4)
                            DESERIALIZATION_DATA(&freq, freqLen)
                            DESERIALIZATION_DATA(&timeSpanLen, 4)
                            DESERIALIZATION_DATA(&timeSpan, timeSpanLen)
                            SectionProperty property;
                        property.domain = domain;
                        property.category = category;
                        property.data_type = dataType;
                        property.frequency_num = freq;
                        property.time_span_ms = timeSpan;

                        int numLen;
                        int32_t num;
                        DESERIALIZATION_DATA(&numLen, 4)
                            DESERIALIZATION_DATA(&num, numLen)
                            SectionData sec_data;
                        sec_data.size = num;
                        sec_data.data = nullptr;

                        auto section = std::make_shared<Section>(property, num, nullptr);
                        auto pair_data = section->getData<void>();
                        DESERIALIZATION_DATA((void*)pair_data.first, num);
                        sections[domain].push_back(section);
                    }
            }

#undef DESERIALIZATION_DATA
    }
private:
    GlobalConfig global;
    std::map< Domain, std::vector< std::shared_ptr<Section> >> sections;    // 按域组织的段列表。
    std::shared_ptr<void> _keep_alive;
};

typedef HQSigMF SignalData;
