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
                result_ = ok ? "Submitted. Check system notifications." : message;
            };
            try {
                diagnostic(("Install requested " + entry.id).c_str(), 0);
                if (entry.type == ContentType::Avatar) installAvatar(entry, done);
                else installTheme(entry, done);
            } catch (const std::exception&) {
                done(false, diagnostic("Installation worker failed", EIO));
            }
            busy_ = false;
        });
    } catch (const std::system_error& error) {
        busy_ = false;
        diagnostic("Start installation worker", error.code().value());
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
        if (fwrite(buf, 1, n, fout) != n) { ok = false; break; }
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
        if (!entry.fileSize || !isCompletePackage(entry.localPath, entry.fileSize, &package) ||
            package.type != 0x1a) {
            cb(false, diagnostic("Avatar PKG rejected: size, format or type", EINVAL));
            return false;
        }
        std::string error;
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
        cb(false, diagnostic("Unsupported or incomplete avatar file", EINVAL));
        return false;
    }
    bool ok = writableDirectory(AVATAR_DEST) && copyFile(entry.localPath, dst);
    if (ok) {
        notify("NEBULA HUB",
               std::string("Avatar ready! Open Avatar Changer to apply ") + entry.name);
        cb(true, dst);
    } else {
        cb(false, diagnostic("Copy avatar", errno));
    }
    return ok;
}

// ─────────────────────────────────────────────
//  Theme install
// ─────────────────────────────────────────────

bool Installer::installTheme(const ContentEntry& entry,
                              std::function<void(bool,std::string)> cb) {
    // Themes come as _1 (stub) + _2 (payload) pair
    // Both must be in /data/pkg/ before triggering install
    if (entry.localStubPath.empty() || entry.localPayloadPath.empty()) {
        cb(false, "Files not downloaded");
        return false;
    }

    PackageInfo first, second;
    if (entry.fileFormat != "pkg_split" || !entry.stubSize || !entry.payloadSize ||
        !isCompletePackage(entry.localStubPath, entry.stubSize, &first) ||
        !isCompletePackage(entry.localPayloadPath, entry.payloadSize, &second) ||
        first.contentId != second.contentId ||
        !(first.type == 0x1c || (first.type == 0x1b && first.iroTag == 2)) ||
        second.type != 0x1b || second.iroTag != 2) {
        cb(false, diagnostic("Theme PKG pair rejected: size, format or type", EINVAL));
        return false;
    }
    if (!writableDirectory(PKG_INSTALL_DIR)) {
        cb(false, "System package directory is not writable");
        return false;
    }
    // Copy to pkg dir with correct names
    std::string stub    = std::string(PKG_INSTALL_DIR) + entry.id + "_1.pkg";
    std::string payload = std::string(PKG_INSTALL_DIR) + entry.id + "_2.pkg";

    if (!copyFile(entry.localStubPath, stub) ||
        !copyFile(entry.localPayloadPath, payload) ||
        !isCompletePackage(stub, entry.stubSize) || !isCompletePackage(payload, entry.payloadSize)) {
        cb(false, diagnostic("Stage theme pair", errno));
        return false;
    }

    // Submit the stub after both parts are staged.
    std::string error;
    bool ok = launchPkgInstall(stub, error);
    if (ok) {
        notify("NEBULA HUB",
               std::string("Installing: ") + entry.name + "\nGo to Settings → Themes");
        cb(true, "");
    } else {
        cb(false, error);
    }
    return ok;
}

// ─────────────────────────────────────────────
//  PKG install trigger (same method as 4PT / FPKGi)
// ─────────────────────────────────────────────

bool Installer::launchPkgInstall(const std::string& pkgPath, std::string& error) {
    int rc = sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_APP_INST_UTIL);
    if (rc < 0) {
        error = diagnostic("Load AppInstUtil module", rc);
        return false;
    }
    rc = sceAppInstUtilInitialize();
    if (rc < 0) {
        error = diagnostic("Initialize AppInstUtil", rc);
        return false;
    }
    diagnostic("Calling AppInstUtil to submit PKG", 0);
    rc = sceAppInstUtilAppInstallPkg(pkgPath.c_str(), nullptr);
    diagnostic("AppInstUtil submit PKG", rc);
    sceAppInstUtilTerminate();
    if (rc < 0) error = diagnostic("AppInstUtil rejected PKG", rc);
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
