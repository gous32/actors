#pragma once

#include "command.h"

namespace NYdb {
namespace NConsoleClient {

extern const TString defaultTokenFile;

class TClientCommandRootBase : public TClientCommandTree {
public:
    TClientCommandRootBase(const TString& name);

    bool TimeRequests;
    bool ProgressRequests;
    TString Address;
    TString Token;
    TString TokenFile;
    TString CaCertsFile;

    virtual void Config(TConfig& config) override;
    virtual void Parse(TConfig& config) override;
    void SetCustomUsage(TConfig& config) override;
    void SetFreeArgs(TConfig& config) override;

protected:
    void ParseToken(TString& token, TString& tokenFile, const TString& envName, bool useDefaultToken = false);
    void ParseProtocol(TConfig& config);
    void ParseCaCerts(TConfig& config);
    virtual void ParseCredentials(TConfig& config);
    virtual void ParseAddress(TConfig& config) = 0;
};

}
}
