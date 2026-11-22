#pragma once

#include <cstdint>
#include <functional>

using AppInstInitialize = int32_t (*)();
using AppInstSubmit = int32_t (*)(const char*, void*);

struct InstallSymbol {
    void* address = nullptr;
    int32_t code = 0;
    bool executable = false;
};

struct InstallService {
    std::function<int32_t()> load;
    std::function<InstallSymbol(const char*)> resolve;
};

struct InstallResult {
    bool accepted = false;
    int32_t code = 0;
    const char* failure = nullptr;
    bool submitReturned = false;
};

using InstallTrace = std::function<void(const char* operation, bool returned, int32_t code)>;

// Every successful initialization is paired with one termination, including
// local cancellation. Termination cannot undo an already accepted request.
inline InstallResult requestPackage(const InstallService& service, const char* path,
                                    const std::function<bool()>& beforeSubmit,
                                    const InstallTrace& trace) {
    trace("Ensure AppInstUtil module", false, 0);
    const auto loaded = service.load();
    trace("Ensure AppInstUtil module", true, loaded);
    if (loaded < 0) return {false, loaded, "Ensure AppInstUtil module"};

    const auto resolve = [&](const char* symbol) {
        trace(symbol, false, 0);
        const auto found = service.resolve(symbol);
        trace(symbol, true, found.code);
        return found;
    };
    const auto initialize = resolve("sceAppInstUtilInitialize");
    const auto valid = [](const InstallSymbol& symbol) {
        return symbol.code == 0 && symbol.address && symbol.executable;
    };
    if (!valid(initialize))
        return {false, initialize.code, "Resolve executable sceAppInstUtilInitialize"};
    const auto terminate = resolve("sceAppInstUtilTerminate");
    if (!valid(terminate))
        return {false, terminate.code, "Resolve executable sceAppInstUtilTerminate"};
    const auto submit = resolve("sceAppInstUtilAppInstallPkg");
    if (!valid(submit))
        return {false, submit.code, "Resolve executable sceAppInstUtilAppInstallPkg"};

    trace("Initialize AppInstUtil service", false, 0);
    const auto initialized = reinterpret_cast<AppInstInitialize>(initialize.address)();
    trace("Initialize AppInstUtil service", true, initialized);
    if (initialized != 0) return {false, initialized, "Initialize AppInstUtil service"};

    InstallResult result{false, 0, "Prepare or cancel submit"};
    try {
        if (beforeSubmit()) {
            trace("Submit PKG", false, 0);
            const auto submitted = reinterpret_cast<AppInstSubmit>(submit.address)(path, nullptr);
            trace("Submit PKG", true, submitted);
            result = {submitted == 0, submitted, submitted == 0 ? nullptr : "Submit PKG", true};
        }
    } catch (...) {
        trace("Terminate AppInstUtil service", false, 0);
        const auto terminated = reinterpret_cast<AppInstInitialize>(terminate.address)();
        trace("Terminate AppInstUtil service", true, terminated);
        throw;
    }
    trace("Terminate AppInstUtil service", false, 0);
    const auto terminated = reinterpret_cast<AppInstInitialize>(terminate.address)();
    trace("Terminate AppInstUtil service", true, terminated);
    return result;
}
