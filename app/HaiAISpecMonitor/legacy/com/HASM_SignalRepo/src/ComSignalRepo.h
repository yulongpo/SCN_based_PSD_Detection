#pragma once
#include "ComStdAfx.h"
#include "radioai/icd/HQSigMF.hpp"
#include "radioai/icd/SignalData.hpp"
#include "core/SignalRepoCore.h"

class ComSignalRepo : public radioai::core::Com
{
public:
    enum EPortDef
    {
        E_IPORT_HQSIGMF                 = 1,
        E_IPORT_SOURCE_DETAIL           = 2,
        E_IPORT_SIGNAL_DETAIL_QUERY_REQ = 3,
        E_IPORT_SPEC_FILE_DEL_REQ = 4,
        E_IPORT_SIGNAL_DEL_REQ = 5,

        E_OPORT_SPECTRUM_FILE_INFOS      = 1,
        E_OPORT_SPECTRUM_FILE_INFO       = 2,
        E_OPORT_SIGNAL_DETAIL_QUERY_RESP = 3,
        E_0PORT_SPEC_FILE_DEL_RESP = 4,
        E_OPORT_SIGNAL_DEL_RESP = 5,
    };

public:
    ComSignalRepo();
    ~ComSignalRepo();

    virtual int init() override;
    virtual int start() override;
    virtual void stop() override;
    virtual void resume() override;
    virtual int reset() override;
    virtual int updateParam(const std::map<std::string, std::string>& params) override;
    virtual int checkParam(const std::map<std::string, std::string>& params) override;
    virtual int process(int portId, radioai::core::ComData* data) override;

private:
    int64_t parseTimeToEpoch(const std::string& datetime);

    SignalRepoCore* _core = nullptr;

    std::string _spectrum_dir;
    std::string _db_path;
    int32_t _file_size = 50;

    SourceDetail  _source_detail;

    bool _inited = false;
};
