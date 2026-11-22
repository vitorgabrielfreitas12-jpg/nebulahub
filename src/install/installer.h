#pragma once
#include "../nebula.h"
#include "../db/database.h"
#include <string>
#include <functional>

// ─────────────────────────────────────────────
//  Installer
//  Handles both avatar (.xavatar) and theme PKG installation
// ─────────────────────────────────────────────

class Installer {
public:
    Installer();

    // Avatar: copy .xavatar file to /data/AVATARS/ and notify user
    // FPKG avatar packs are submitted to AppInstUtil.
    bool installAvatar(const ContentEntry& entry,
                       std::function<void(bool,std::string)> cb);

    // Stage both theme parts and submit the stub to AppInstUtil.
    bool installTheme(const ContentEntry& entry,
                      std::function<void(bool,std::string)> cb);

    // Send PS4 notification (top-right popup)
    void notify(const std::string& title, const std::string& msg);

private:
    bool copyFile(const std::string& src, const std::string& dst);
    bool launchPkgInstall(const std::string& pkgPath);
    void ensureDir(const std::string& path);
};
