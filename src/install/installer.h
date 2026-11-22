#pragma once
#include "../nebula.h"
#include "../db/database.h"
#include <string>
#include <functional>
#include <atomic>
#include <mutex>
#include <thread>

// ─────────────────────────────────────────────
//  Installer
//  Handles both avatar (.xavatar) and theme PKG installation
// ─────────────────────────────────────────────

class Installer {
public:
    Installer() = default;
    ~Installer();
    bool submit(ContentEntry entry);
    bool busy() const { return busy_; }
    std::string takeResult(const std::string& id);

private:

    // Avatar: copy .xavatar file to /data/AVATARS/ and notify user
    // FPKG avatar packs are submitted to AppInstUtil.
    bool installAvatar(const ContentEntry& entry,
                       std::function<void(bool,std::string)> cb);

    // Stage both theme parts and submit the stub to AppInstUtil.
    bool installTheme(const ContentEntry& entry,
                      std::function<void(bool,std::string)> cb);

    // Send PS4 notification (top-right popup)
    void notify(const std::string& title, const std::string& msg);

    std::thread worker_;
    std::atomic<bool> busy_{false};
    std::mutex mutex_;
    std::string resultId_;
    std::string result_;

    bool copyFile(const std::string& src, const std::string& dst);
    bool launchPkgInstall(const std::string& pkgPath, std::string& error);
};
