// NEBULA HUB — Installer
// Avatar path: copy xavatar → /data/AVATARS/
// Theme path: stage the PKG pair in /data/pkg/ and submit the stub.

#include "installer.h"
#include "package.h"
#include "../diagnostics.h"

#include <orbis/AppInstUtil.h>
#include <orbis/Sysmodule.h>
#include <orbis/libkernel.h>

#include <cstdio>
#include <sys/stat.h>

Installer::~Installer() {
    if (worker_.joinable()) worker_.join();
}

bool Installer::submit(ContentEntry entry) {
    if (busy_) return false;
    if (worker_.joinable()) worker_.join();
    busy_ = true;
    try {
        worker_ = std::thread([this, entry = std::move(entry)] {
            const auto done = [this, &entry](bool ok, const std::string& message) {
                std::lock_guard<std::mutex> lock(mutex_);
                resultId_ = entry.id;
                result_ = ok && message.empty() ? "Pedido enviado. Confira as notificações do PS4." : message;
            };
            try {
                diagnostic("installer", "Install requested", ErrorDomain::Event, 0, entry.id.c_str());
                if (entry.type == ContentType::Avatar) installAvatar(entry, done);
                else installTheme(entry, done);
            } catch (const std::exception&) {
                done(false, diagnostic("installer", "Installation worker failed", ErrorDomain::Application, EIO));
            }
            busy_ = false;
        });
    } catch (const std::system_error& error) {
        busy_ = false;
        diagnostic("installer", "Start worker", ErrorDomain::Posix, error.code().value());
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

bool Installer::copyFile(const std::string& src, const std::string& dst) {
    FILE* fin  = fopen(src.c_str(), "rb");
    if (!fin) return false;
    const std::string partial = dst + ".part";
    FILE* fout = fopen(partial.c_str(), "wb");
    if (!fout) { fclose(fin); return false; }

    char buf[64 * 1024];
    size_t n;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof(buf), fin)) > 0) {
        if (const auto failure = writeStorage(fout, buf, n)) { errno = failure.code; ok = false; break; }
    }
    ok = ok && !ferror(fin);
    fclose(fin);
    if (fclose(fout) != 0) ok = false;
    if (ok) ok = rename(partial.c_str(), dst.c_str()) == 0;
    if (!ok) remove(partial.c_str());
    return ok;
}

// ─────────────────────────────────────────────
//  Avatar install
// ─────────────────────────────────────────────

namespace {
std::string packageError(const std::string& path, uint64_t expected, PackageInfo& info) {
    int code = 0;
    const auto status = inspectPackage(path, expected, &info, &code);
    if (status == PackageStatus::Valid) return {};
    const auto domain = code ? ErrorDomain::Posix : ErrorDomain::Application;
    diagnostic("installer", "Validate local package", domain,
               code ? code : static_cast<int>(status), path.c_str());
    switch (status) {
        case PackageStatus::Missing: return "Baixe o pacote antes de instalar.";
        case PackageStatus::Incomplete: return "Download incompleto. Não é possível instalar.";
        case PackageStatus::Invalid: return "Arquivo PKG inválido ou incompatível.";
        default: return code ? "Falha ao ler o PKG: " + std::string(strerror(code)) : "Falha ao ler o PKG.";
    }
}

std::string unsupportedType(const std::string& path) {
    diagnostic("installer", "Unsupported package type", ErrorDomain::Application, 0, path.c_str());
    return "Tipo de PKG incompatível.";
}
}

bool Installer::installAvatar(const ContentEntry& entry,
                               std::function<void(bool,std::string)> cb) {
    if (entry.localPath.empty()) {
        cb(false, "File not downloaded");
        return false;
    }

    if (entry.fileFormat == "xavatar_fpkg") {
        // This is a BULK FPKG (F1nnish pack) — we install it as a PKG
        // then the user applies individual avatars from Avatar Changer
        PackageInfo package;
        auto error = packageError(entry.localPath, entry.fileSize, package);
        if (error.empty() && package.type != 0x1a) error = unsupportedType(entry.localPath);
        if (!error.empty()) {
            cb(false, error);
            return false;
        }
        bool ok = launchPkgInstall(entry.localPath, error);
        if (ok) {
            notify("NEBULA HUB",
                   "Avatar pack submitted. Open Avatar Changer (LAPY) to apply.");
            cb(true, "");
        } else {
            cb(false, error);
        }
        return ok;
    }

    // Single .xavatar file → copy to /data/AVATARS/
    std::string filename = entry.id + ".xavatar";
    std::string dst = std::string(AVATAR_DEST) + filename;
    struct stat info{};
    if (entry.fileFormat != "xavatar" || !entry.fileSize ||
        stat(entry.localPath.c_str(), &info) != 0 || !S_ISREG(info.st_mode) ||
        static_cast<uint64_t>(info.st_size) != entry.fileSize) {
        cb(false, diagnostic("installer", "Unsupported or incomplete avatar file", ErrorDomain::Application, 0, entry.localPath.c_str()));
        return false;
    }
    bool ok = writableDirectory(AVATAR_DEST) && copyFile(entry.localPath, dst);
    if (ok) {
        notify("NEBULA HUB",
               std::string("Avatar ready! Open Avatar Changer to apply ") + entry.name);
        cb(true, dst);
    } else {
        cb(false, diagnostic("installer", "Copy avatar", ErrorDomain::Posix, errno, dst.c_str()));
    }
    return ok;
}

// ─────────────────────────────────────────────
//  Theme install
// ─────────────────────────────────────────────

bool Installer::installTheme(const ContentEntry& entry,
                              std::function<void(bool,std::string)> cb) {
    if (entry.fileFormat == "pkg_theme") {
        PackageInfo info;
        auto error = packageError(entry.localPath, entry.fileSize, info);
        if (error.empty() && !isThemePayload(info))
            error = unsupportedType(entry.localPath);
        const bool accepted = error.empty() && launchPkgInstall(entry.localPath, error);
        cb(accepted, error);
        return accepted;
    }
    if (entry.fileFormat != "pkg_split") {
        cb(false, "Formato de tema incompatível.");
        return false;
    }

    PackageInfo first, second;
    auto error = packageError(entry.localStubPath, entry.stubSize, first);
    if (!error.empty()) {
        cb(false, "Parte 1: " + error);
        return false;
    }
    error = packageError(entry.localPayloadPath, entry.payloadSize, second);
    if (!error.empty()) {
        cb(false, "Parte 2: " + error);
        return false;
    }
    if (!isThemePair(first, second)) {
        cb(false, unsupportedType(entry.localStubPath));
        return false;
    }
    if (!writableDirectory(PKG_INSTALL_DIR, &error)) {
        cb(false, error);
        return false;
    }

    const auto pairKey = entry.id + ":" + first.contentId;
    const bool secondPart = pendingTheme_ == pairKey;
    const auto& source = secondPart ? entry.localPayloadPath : entry.localStubPath;
    const auto staged = std::string(PKG_INSTALL_DIR) + entry.id + (secondPart ? "_2.pkg" : "_1.pkg");
    if (!copyFile(source, staged)) {
        cb(false, diagnostic("installer", "Stage theme component", ErrorDomain::Posix, errno, staged.c_str()));
        return false;
    }
    error = packageError(staged, secondPart ? entry.payloadSize : entry.stubSize, secondPart ? second : first);
    if (!error.empty() || !launchPkgInstall(staged, error)) {
        cb(false, std::string(secondPart ? "Parte 2: " : "Parte 1: ") + error);
        return false;
    }
    pendingTheme_ = secondPart ? "" : pairKey;
    cb(true, secondPart ? "Parte 2 enviada. Confira as notificações do PS4."
                        : "Parte 1 enviada. Aguarde a instalação no PS4 antes de selecionar Install novamente.");
    return true;
}

// ─────────────────────────────────────────────
//  PKG install trigger (same method as 4PT / FPKGi)
// ─────────────────────────────────────────────

bool Installer::launchPkgInstall(const std::string& pkgPath, std::string& error) {
    int rc = sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_APP_INST_UTIL);
    if (rc < 0) {
        error = diagnostic("installer", "Load AppInstUtil module", ErrorDomain::Orbis, rc);
        return false;
    }
    rc = sceAppInstUtilInitialize();
    if (rc < 0) {
        error = diagnostic("installer", "Initialize AppInstUtil", ErrorDomain::Orbis, rc);
        return false;
    }
    diagnostic("installer", "Submit PKG", ErrorDomain::Event, 0, pkgPath.c_str());
    rc = sceAppInstUtilAppInstallPkg(pkgPath.c_str(), nullptr);
    const auto result = diagnostic("installer", rc < 0 ? "AppInstUtil rejected PKG" : "AppInstUtil accepted request",
                                   ErrorDomain::Orbis, rc, pkgPath.c_str());
    sceAppInstUtilTerminate();
    if (rc < 0) error = result;
    return rc >= 0;
}

// ─────────────────────────────────────────────
//  Notification
// ─────────────────────────────────────────────

void Installer::notify(const std::string& title, const std::string& msg) {
    // PS4 toast notification — same API used by most homebrews
    OrbisNotificationRequest request{};
    request.type = NotificationRequest;
    request.targetId = -1;
    snprintf(request.message, sizeof(request.message), "%s: %s", title.c_str(), msg.c_str());
    sceKernelSendNotificationRequest(0, &request, sizeof(request), 0);
    printf("[NEBULA HUB] %s: %s\n", title.c_str(), msg.c_str());
}
