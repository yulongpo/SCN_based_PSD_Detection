#pragma once
#include "ComStdAfx.h"
#include "radioai/icd/HQSigMF.hpp"
#include "radioai/icd/SignalData.hpp"
#include "core/SignalWhitelistProcessor.h"
#include <string>

class ComSignalWhitelist : public radioai::core::Com
{
public:
    enum EPortDef
    {
        E_IPORT_HQSIGMF           = 1,
        E_IPORT_WHITELIST_OP_REQ = 2,
        E_OPORT_WHITELISTS = 1,
        E_OPORT_HQSIGMF           = 2,
        E_OPORT_WHITELIST_OP_RESP = 3,
    };

public:
    ComSignalWhitelist();
    ~ComSignalWhitelist();

    virtual int init() override;
    virtual int start() override;
    virtual void stop() override;
    virtual int reset() override;
    virtual int checkParam(const std::map<std::string, std::string>& params) override;
    virtual int updateParam(const std::map<std::string, std::string>& params) override;
    virtual int process(int portId, radioai::core::ComData* data) override;

private:
    void reportWhitelists();
    int persistWhitelists();

    SignalWhitelistProcessor* _processor = nullptr;
    std::string _whitelist_json_path;
    bool _inited = false;
};
