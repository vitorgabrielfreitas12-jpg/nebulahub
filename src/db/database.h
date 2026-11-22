#pragma once
#include "../nebula.h"
#include <string>
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <functional>

// ─────────────────────────────────────────────
//  Content entry — avatar or theme
// ─────────────────────────────────────────────

struct ContentEntry {
    std::string  id;
    std::string  name;
    ContentType  type;
    std::string  category;
    std::string  subcategory;
    bool         animated   = false;
    std::string  previewUrl;
    std::string  downloadUrl;        // for avatars: xavatar fpkg url
    std::string  pkgStubUrl;         // for themes: _1.pkg url
    std::string  pkgPayloadUrl;      // for themes: _2.pkg url
    uint64_t     fileSize   = 0;
    uint64_t     stubSize   = 0;
    uint64_t     payloadSize= 0;
    std::string  fileFormat;         // "xavatar_fpkg" | "pkg_split" | "ctmthm"
    std::string  source;
    std::string  installMethod;      // "avatar_changer" | "pkg_install"
    std::vector<std::string> tags;
    InstallState installState = InstallState::None;
    bool         favorite    = false;
    std::string  localPath;          // path after download
    std::string  localStubPath;
    std::string  localPayloadPath;
};

// ─────────────────────────────────────────────
//  Database
// ─────────────────────────────────────────────

class Database {
public:
    Database();

    void load(const std::string& path);
    void save(const std::string& path);
    bool importManifest(const std::string& jsonText);
    bool loadManifestFile(const std::string& path);
    void replaceCatalog(Database&& snapshot);
    bool empty() const { return entries_.empty(); }
    uint64_t revision() const { return revision_; }

    // Queries
    std::vector<ContentEntry*> all();
    std::vector<ContentEntry*> byType(ContentType t);
    std::vector<ContentEntry*> byCategory(ContentType t, const std::string& cat);
    std::vector<ContentEntry*> favorites();
    std::vector<ContentEntry*> installed();
    std::vector<ContentEntry*> search(const std::string& query, ContentType filter, bool animatedOnly);

    ContentEntry*              byId(const std::string& id);

    void toggleFavorite(const std::string& id);
    void setInstallState(const std::string& id, InstallState s, const std::string& localPath = "");

    // Category helpers
    std::vector<std::string> avatarCategories();
    std::vector<std::string> themeCategories();

    size_t totalAvatars() const;
    size_t totalThemes()  const;

private:
    std::unordered_map<std::string, ContentEntry> entries_;
    std::unordered_set<std::string> favoriteIds_;
    uint64_t revision_ = 0;
    size_t avatarCount_ = 0;
    uint64_t     manifestTimestamp_ = 0;

    bool parseJson(const std::string& json, bool local);
    std::string  serializeJson() const;

    // Simple lowercase helper for search
    static std::string lower(const std::string& s);
    static bool  contains(const std::string& haystack, const std::string& needle);
};
