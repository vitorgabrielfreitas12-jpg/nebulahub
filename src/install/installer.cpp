#include "installer.h"
#include "package.h"
#include "prepare.h"
#include "service.h"
#include "theme_progress.h"
#include "../diagnostics.h"

#include <orbis/AppInstUtil.h>
#include <orbis/Sysmodule.h>
#include <orbis/libkernel.h>

#include <algorithm>
#include <array>
#include <string_view>
#include <type_traits>

namespace {
constexpr auto submitted = "Pedido enviado. Confira as notificações do PS4.";

std::string logInstall(const char* operation, ErrorDomain domain, int code, const std::string& context) {
    return diagnostic("installer", operation, domain, code, context.c_str(), true);
}

void logOperation(const char* operation, bool returned, int code, ErrorDomain domain,
                  const std::string& context) {
    const std::string name = std::string(operation) + (returned ? " returned" : " begin");
    logInstall(name.c_str(), returned ? domain : ErrorDomain::Event, code, context);
}

std::string packageError(const std::string& path, uint64_t expected, PackageInfo& info,
                         const std::string& context) {
    logInstall("Validate PKG begin", ErrorDomain::Event, 0, context + " path=" + path);
    const auto result = inspectPackageDetails(path, expected);
    logInstall(result.reason, result.ioError ? ErrorDomain::Posix : ErrorDomain::Event,
               result.ioError, context + " path=" + path + " " + describePackage(result, expected));
    if (result) {
        info = result.info;
        return {};
    }
    switch (result.status) {
        case PackageStatus::Missing: return "Baixe o pacote antes de instalar.";
        case PackageStatus::Incomplete: return "Download incompleto. Não é possível instalar.";
        case PackageStatus::Invalid: return "Arquivo PKG inválido ou incompatível.";
        default: return result.ioError ? "Falha ao ler o PKG: " + std::string(strerror(result.ioError)) :
                                        "Falha ao ler o PKG.";
    }
}

std::string unsupportedType(const std::string& context) {
    logInstall("PKG type or content ID mismatch", ErrorDomain::Application, 0, context);
    return "Tipo ou identificador de PKG incompatível.";
}

std::string prepare(const std::string& source, const std::string& destination,
                    const char* directory, const std::string& context) {
    const auto detail = context + " destination=" + destination;
    logInstall("Probe staging directory begin", ErrorDomain::Event, 0, detail);
    const auto probe = probeDirectory(directory);
    logInstall("Probe staging directory returned", ErrorDomain::Posix, probe.code, detail);
    if (probe) return logInstall(probe.operation, ErrorDomain::Posix, probe.code, detail);
    const auto staged = stagePackage(source, destination, [&](const char* operation, bool returned, int code) {
        logOperation(operation, returned, code, ErrorDomain::Posix, detail);
    });
    if (staged) return logInstall(staged.operation, ErrorDomain::Posix, staged.code, detail);
    return {};
}

static_assert(std::is_same_v<decltype(&sceAppInstUtilInitialize), AppInstInitialize>);
static_assert(std::is_same_v<decltype(&sceAppInstUtilTerminate), AppInstInitialize>);
static_assert(std::is_same_v<decltype(&sceAppInstUtilAppInstallPkg), AppInstSubmit>);

struct AppInstModule {
    OrbisKernelModule handle{};
    OrbisKernelModuleInfo info{};
    bool found = false;
    int32_t error = 0;
};

AppInstModule findAppInstModule(const std::string& context) {
    AppInstModule module;
    std::array<OrbisKernelModule, 256> handles{};
    size_t count = 0;
    logInstall("sceKernelGetModuleList begin", ErrorDomain::Event, 0, context);
    module.error = sceKernelGetModuleList(handles.data(), handles.size(), &count);
    logInstall("sceKernelGetModuleList returned", ErrorDomain::Orbis, module.error,
               context + " count=" + std::to_string(count));
    if (module.error != 0) return module;

    for (size_t i = 0; i < std::min(count, handles.size()); ++i) {
        OrbisKernelModuleInfo info{};
        info.size = sizeof(info);
        const auto handle = " handle=" + std::to_string(handles[i]);
        logInstall("sceKernelGetModuleInfo begin", ErrorDomain::Event, 0, context + handle);
        const auto rc = sceKernelGetModuleInfo(handles[i], &info);
        if (rc != 0) {
            logInstall("sceKernelGetModuleInfo returned", ErrorDomain::Orbis, rc, context + handle);
            continue;
        }
        const std::string_view name(info.name, strnlen(info.name, sizeof(info.name)));
        const auto basename = name.substr(name.find_last_of('/') + 1);
        logInstall("sceKernelGetModuleInfo returned", ErrorDomain::Orbis, rc,
                   context + handle + " name=" + std::string(basename));
        if (basename == "libSceAppInstUtil.sprx" || basename == "libSceAppInstUtil")
            return {handles[i], info, true, 0};
    }
    logInstall("AppInstUtil absent from module list", ErrorDomain::Application, 0, context);
    return module;
}

bool executableAddress(const OrbisKernelModuleInfo& module, const void* address) {
    if (!address) return false;
    const auto value = reinterpret_cast<uintptr_t>(address);
    const auto count = std::min<size_t>(module.segmentCount, std::size(module.segmentInfo));
    for (size_t i = 0; i < count; ++i) {
        const auto& segment = module.segmentInfo[i];
        const auto start = reinterpret_cast<uintptr_t>(segment.address);
        if (start && (segment.prot & ORBIS_KERNEL_PROT_CPU_EXEC) &&
            value >= start && value - start < segment.size) return true;
    }
    return false;
}

InstallResult launchPackage(const std::string& path, const std::string& context,
                            const std::function<bool()>& beforeSubmit) {
    const auto detail = context + " path=" + path;
    AppInstModule module;
    bool enumerated = false;
    const InstallService service{
        [&] {
            static bool loaded = false;  // One Installer worker at a time. Keep the module resident.
            if (loaded) {
                logInstall("Use resident AppInstUtil module", ErrorDomain::Event, 0, detail);
                return int32_t{0};
            }
            logInstall("sceSysmoduleLoadModuleInternal begin", ErrorDomain::Event, 0, detail);
            const auto rc = static_cast<int32_t>(
                sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_APP_INST_UTIL));
            logInstall("sceSysmoduleLoadModuleInternal returned", ErrorDomain::Orbis, rc, detail);
            loaded = rc >= 0;
            return rc;
        },
        [&](const char* name) {
            if (!enumerated) {
                module = findAppInstModule(detail);
                enumerated = true;
            }
            if (!module.found) return InstallSymbol{nullptr, module.error, false};
            void* address = nullptr;
            const auto rc = sceKernelDlsym(module.handle, name, &address);
            const bool executable = rc == 0 && executableAddress(module.info, address);
            if (rc == 0 && !executable)
                logInstall(address ? "Resolved symbol outside executable segment" : "Resolved symbol address is null",
                           ErrorDomain::Application, 0, detail + " symbol=" + name);
            return InstallSymbol{address, rc, executable};
        }
    };
    return requestPackage(service, path.c_str(), beforeSubmit,
                          [&](const char* name, bool returned, int32_t rc) {
        logOperation(name, returned, rc, ErrorDomain::Orbis, detail);
    });
}

std::string installError(const InstallResult& result, const std::string& context) {
    return logInstall(result.failure, result.code ? ErrorDomain::Orbis : ErrorDomain::Application,
                      result.code, context);
}

std::string progressKey(const std::string& entry, const std::string& contentId) {
    constexpr char hex[] = "0123456789abcdef";
    std::string key = entry + ':';
    for (const unsigned char byte : contentId) {
        key += hex[byte >> 4];
        key += hex[byte & 0xf];
    }
    return key;
}

void notifyAvatar(const char* message, const std::string& context) {
    OrbisNotificationRequest request{};
    request.type = NotificationRequest;
    request.targetId = -1;
    snprintf(request.message, sizeof(request.message), "NEBULA HUB: %s", message);
    logInstall("Send avatar notification begin", ErrorDomain::Event, 0, context);
    const auto rc = sceKernelSendNotificationRequest(0, &request, sizeof(request), 0);
    logInstall("Send avatar notification returned", ErrorDomain::Orbis, rc, context);
}
}

Installer::~Installer() {
    if (worker_.joinable()) worker_.join();
}

bool Installer::submit(ContentEntry entry, InstallCommand command) {
    if (busy_.exchange(true)) return false;
    if (worker_.joinable()) worker_.join();
    try {
        worker_ = std::thread([this, entry = std::move(entry), command] {
            std::string result;
            try {
                const auto context = "entry=" + entry.id;
                logInstall("Install requested", ErrorDomain::Event, 0,
                           context + " action=" + std::to_string(static_cast<int>(command)));
                if (command != InstallCommand::Request &&
                    (entry.type != ContentType::Theme || entry.fileFormat != "pkg_split")) {
                    result = "Esta ação exige um tema em duas partes.";
                } else {
                    result = entry.type == ContentType::Avatar ? installAvatar(entry) :
                                                                 installTheme(entry, command);
                }
            } catch (const std::exception& error) {
                result = logInstall("Installation worker failed", ErrorDomain::Application, 0,
                                    entry.id + " " + error.what());
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                resultId_ = entry.id;
                result_ = std::move(result);
            }
            busy_ = false;
        });
    } catch (const std::system_error& error) {
        busy_ = false;
        logInstall("Start worker", ErrorDomain::Posix, error.code().value(), {});
        return false;
    }
    return true;
}

std::string Installer::takeResult(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string result;
    if (resultId_ == id) result.swap(result_);
    return result;
}

std::string Installer::installAvatar(const ContentEntry& entry) {
    const auto context = "entry=" + entry.id;
    if (entry.localPath.empty()) return "Baixe o arquivo antes de instalar.";
    if (entry.fileFormat == "xavatar_fpkg") {
        PackageInfo info;
        const auto error = packageError(entry.localPath, entry.fileSize, info, context);
        if (!error.empty()) return error;
        if (!matchesPackageRole(info, PackageRole::AvatarPack)) return unsupportedType(context);
        const auto result = launchPackage(entry.localPath, context + " content_id=" + info.contentId,
                                          [] { return true; });
        if (!result.accepted) return installError(result, context);
        notifyAvatar("Avatar pack submitted. After installation, open Avatar Changer (LAPY).", context);
        return submitted;
    }

    uint64_t bytes = 0;
    FileInput input(entry.localPath.c_str());
    if (entry.fileFormat != "xavatar" || !entry.fileSize || !input ||
        !input.measure(bytes) || bytes != entry.fileSize)
        return logInstall("Unsupported or incomplete avatar file", ErrorDomain::Application, 0, context);
    const auto destination = std::string(AVATAR_DEST) + entry.id + ".xavatar";
    const auto error = prepare(entry.localPath, destination, AVATAR_DEST, context);
    if (!error.empty()) return error;
    notifyAvatar("Avatar ready to apply in Avatar Changer.", context);
    return "Avatar copiado. Abra o Avatar Changer para aplicá-lo.";
}

std::string Installer::installTheme(const ContentEntry& entry, InstallCommand command) {
    const auto context = "entry=" + entry.id;
    if (entry.fileFormat == "pkg_theme") {
        PackageInfo info;
        const auto error = packageError(entry.localPath, entry.fileSize, info, context);
        if (!error.empty()) return error;
        if (!isThemePayload(info)) return unsupportedType(context);
        const auto result = launchPackage(entry.localPath, context + " content_id=" + info.contentId,
                                          [] { return true; });
        return result.accepted ? submitted : installError(result, context);
    }
    if (entry.fileFormat != "pkg_split") return "Formato de tema incompatível.";

    ThemeProgress progress(DATA_DIR "theme-install.state");
    if (command == InstallCommand::ClearProgress) {
        logInstall("Clear local theme request state begin", ErrorDomain::Event, 0, context);
        const auto rc = progress.clear();
        logInstall("Clear local theme request state returned", ErrorDomain::Posix, rc, context);
        return rc == 0 ? "Registro local limpo. Confira o estado do tema no PS4." :
                         "Falha ao limpar o registro local. Consulte diagnostics.log.";
    }

    PackageInfo first, second;
    auto error = packageError(entry.localStubPath, entry.stubSize, first, context + " part=1");
    if (!error.empty()) return "Parte 1: " + error;
    error = packageError(entry.localPayloadPath, entry.payloadSize, second, context + " part=2");
    if (!error.empty()) return "Parte 2: " + error;
    if (!isThemePair(first, second)) return unsupportedType(context);

    const auto key = progressKey(entry.id, first.contentId);
    const bool confirmed = command == InstallCommand::ConfirmFirst;
    const auto action = progress.next(key, confirmed);
    switch (action) {
        case ThemeAction::ConfirmFirst: return "Parte 1 pendente. Confirme no PS4 e pressione △ em Install.";
        case ThemeAction::CheckSecond: return "Parte 2 já solicitada. Veja o resultado nas notificações do PS4.";
        case ThemeAction::OtherTheme: return "Outro tema está pendente. Confira o PS4 antes de limpar o registro.";
        case ThemeAction::Reset: return "Registro local ilegível. Confira o PS4 antes de limpá-lo.";
        case ThemeAction::First: if (confirmed) return "Pressione ✕ em Install para solicitar a parte 1."; break;
        case ThemeAction::Second: break;
    }

    const bool partTwo = action == ThemeAction::Second;
    const auto& source = partTwo ? entry.localPayloadPath : entry.localStubPath;
    const auto& original = partTwo ? second : first;
    const auto expected = partTwo ? entry.payloadSize : entry.stubSize;
    const auto role = partTwo ? PackageRole::ThemePayload : PackageRole::ThemeFirst;
    const auto detail = context + (partTwo ? " part=2" : " part=1") + " content_id=" + original.contentId;
    const auto staged = std::string(PKG_INSTALL_DIR) + entry.id + (partTwo ? "_2.pkg" : "_1.pkg");
    error = prepare(source, staged, PKG_INSTALL_DIR, detail);
    if (!error.empty()) return error;
    PackageInfo copy;
    error = packageError(staged, expected, copy, detail + " staged=true");
    if (!error.empty()) return error;
    if (!matchesPackageRole(copy, role) || copy.contentId != original.contentId ||
        copy.type != original.type || copy.iroTag != original.iroTag)
        return unsupportedType(detail);

    const auto result = requestThemePart(progress, key, partTwo, [&](const auto& beforeSubmit) {
        return launchPackage(staged, detail, beforeSubmit);
    }, [&](const char* operation, bool returned, int32_t value) {
        if (returned) logOperation(operation, true, value, ErrorDomain::Posix, detail);
        else logOperation(operation, false, 0, ErrorDomain::Event,
                          detail + " phase=" + std::to_string(value));
    });
    if (!result.accepted) {
        const auto failure = installError(result, detail);
        if (result.submitReturned)
            return "Pedido retornou erro. Confira o PS4 antes de limpar o registro.";
        return failure;
    }
    return partTwo ? "Parte 2 enviada. Confira o resultado nas notificações do PS4." :
                     "Parte 1 enviada. Confirme a instalação no PS4 antes de pressionar △.";
}
