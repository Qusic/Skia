#include <string>
#include <unordered_map>
#include <functional>
#include <CoreFoundation/CoreFoundation.h>
#include <JavaScriptCore/JavaScriptCore.h>

class config {
private:
    JSGlobalContextRef script_context;
    const std::unordered_map<std::string, std::string> native_functions = {
        {"__skia_primaryAddresses", "__ZL39_JSPrimaryIpv4AddressesFunctionCallbackPK15OpaqueJSContextP13OpaqueJSValueS3_mPKPKS2_PS5_"},
        {"__skia_dnsResolve", "__ZL29_JSDnsResolveFunctionCallbackPK15OpaqueJSContextP13OpaqueJSValueS3_mPKPKS2_PS5_"},
    };
    void create_context();
    void release_context();
    JSStringRef read_script(const std::string &file);
public:
    config() { create_context(); }
    ~config() { release_context(); }
    void execute(const std::function<void(JSGlobalContextRef)> &code);
    std::string evaluate(const std::string &code);
};
