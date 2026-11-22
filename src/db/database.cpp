#include "database.h"
#include "json.hpp"
#include "../net/url.h"
#include "../diagnostics.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>

using nlohmann::json;

Database::Database() = default;

void Database::load(const std::string& path) {
    std::ifstream file(path);
    if (!file) return;
    file.seekg(0, std::ios::end);
    const auto size = file.tellg();
    if (size <= 0 || size > 24 * 1024 * 1024) return;
    file.seekg(0);
    std::string text(static_cast<size_t>(size), '\0');
    if (!file.read(text.data(), text.size()) || !parseJson(text, true))
        diagnostic("catalog", "Read saved catalog", ErrorDomain::Application, 0, path.c_str());
}

void Database::save(const std::string& path) {
    if (path.empty()) return;
    const std::string partial = path + ".tmp";
    std::ofstream file(partial);
    file << serializeJson();
    file.close();
    if (!file || rename(partial.c_str(), path.c_str()) != 0) {
        const int code = errno;
        remove(partial.c_str());
        diagnostic("catalog", "Save catalog", ErrorDomain::Posix, code, path.c_str());
    }
}

bool Database::importManifest(const std::string& jsonText) {
    if (!parseJson(jsonText, false)) return false;
    manifestTimestamp_ = static_cast<uint64_t>(time(nullptr));
    return true;
}

void Database::replaceCatalog(Database&& snapshot) {
    for (auto& [id, entry] : snapshot.entries_) {
        entry.favorite = favoriteIds_.count(id) != 0;
        if (const auto* saved = byId(id)) {
            entry.installState = saved->installState;
            entry.localPath = saved->localPath;
            entry.localStubPath = saved->localStubPath;
            entry.localPayloadPath = saved->localPayloadPath;
        }
    }
    entries_.swap(snapshot.entries_);
    avatarCount_ = snapshot.avatarCount_;
    manifestTimestamp_ = snapshot.manifestTimestamp_;
    ++revision_;
}

bool Database::loadManifestFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    const auto size = file.tellg();
    if (size <= 0 || size > 16 * 1024 * 1024) return false;
    file.seekg(0);
    std::string text(static_cast<size_t>(size), '\0');
    return file.read(text.data(), text.size()) && importManifest(text);
}

bool Database::parseJson(const std::string& text, bool local) {
    const auto document = json::parse(text, nullptr, false);
    if (!document.is_object() || !document.contains("entries") ||
        !document["entries"].is_array() || document["entries"].empty() ||
        document["entries"].size() > 10000) return false;

    std::unordered_map<std::string, ContentEntry> parsed;
    std::unordered_set<std::string> favorites = favoriteIds_;
    uint64_t timestamp = 0;
    size_t avatars = 0;
    const auto validId = [](const std::string& id) {
        return !id.empty() && id.size() <= 100 && id.find_first_not_of(
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") == std::string::npos;
    };
    const auto size = [](const json& item, const char* field) -> uint64_t {
        const auto it = item.find(field);
        if (it == item.end()) return 0;
        if (it->is_number_unsigned()) {
            const auto bytes = it->get<uint64_t>();
            if (bytes <= 16ULL * 1024 * 1024 * 1024 * 1024) return bytes;
        }
        throw std::invalid_argument("invalid byte count");
    };
    try {
        for (const auto& item : document["entries"]) {
            ContentEntry entry;
            entry.id = item.value("id", "");
            if (!validId(entry.id) || parsed.count(entry.id)) return false;
            entry.name = item.value("name", "");
            const auto type = item.value("type", "");
            if (entry.name.empty() || entry.name.size() > 256 || (type != "avatar" && type != "theme"))
                return false;
            entry.type = type == "theme" ? ContentType::Theme : ContentType::Avatar;
            avatars += entry.type == ContentType::Avatar;
            entry.category = item.value("category", "");
            entry.subcategory = item.value("subcategory", "");
            entry.animated = item.value("animated", false);
            entry.previewUrl = item.value("preview_url", "");
            entry.downloadUrl = item.value("download_url", "");
            entry.pkgStubUrl = item.value("pkg_stub_url", "");
            entry.pkgPayloadUrl = item.value("pkg_payload_url", "");
            entry.fileSize = size(item, "file_size");
            entry.stubSize = size(item, "stub_size");
            entry.payloadSize = size(item, "payload_size");
            entry.fileFormat = item.value("file_format", "");
            entry.source = item.value("source", "");
            entry.installMethod = item.value("install_method", "");
            entry.tags = item.value("tags", std::vector<std::string>{});
            for (auto* url : {&entry.downloadUrl, &entry.pkgStubUrl, &entry.pkgPayloadUrl})
                if (!url->empty() && !validHttpsUrl(*url)) url->clear();
            if (entry.previewUrl != "/app0/assets/images/" + entry.id + ".png" &&
                !validHttpsUrl(entry.previewUrl)) entry.previewUrl.clear();
            if (entry.category.size() > 100 || entry.subcategory.size() > 100 || entry.tags.size() > 32 ||
                std::any_of(entry.tags.begin(), entry.tags.end(), [](const auto& tag) { return tag.size() > 100; }))
                return false;
            if (local) {
                const auto state = item.value("install_state", 0);
                if (state == static_cast<int>(InstallState::Done))
                    entry.installState = InstallState::Done;
                entry.favorite = item.value("favorite", false);
                if (entry.favorite) favorites.insert(entry.id);
                entry.localPath = item.value("local_path", "");
                entry.localStubPath = item.value("local_stub_path", "");
                entry.localPayloadPath = item.value("local_payload_path", "");
            } else if (const auto* saved = byId(entry.id)) {
                entry.installState = saved->installState;
                entry.favorite = favoriteIds_.count(entry.id) != 0;
                entry.localPath = saved->localPath;
                entry.localStubPath = saved->localStubPath;
                entry.localPayloadPath = saved->localPayloadPath;
            }
            if (!local && favoriteIds_.count(entry.id)) entry.favorite = true;
            const auto id = entry.id;
            parsed.emplace(id, std::move(entry));
        }
        if (local) {
            timestamp = document.value<uint64_t>("timestamp", 0);
            if (document.contains("favorite_ids")) {
                for (const auto& id : document.at("favorite_ids").get<std::vector<std::string>>())
                    if (validId(id)) favorites.insert(id);
            }
        }
    } catch (const std::exception&) {
        return false;
    }
    for (const auto& id : favorites) {
        const auto it = parsed.find(id);
        if (it != parsed.end()) it->second.favorite = true;
    }
    entries_.swap(parsed);
    favoriteIds_.swap(favorites);
    avatarCount_ = avatars;
    if (local) manifestTimestamp_ = timestamp;
    ++revision_;
    return true;
}

std::string Database::serializeJson() const {
    auto items = json::array();
    for (const auto& [id, entry] : entries_) {
        items.push_back({
            {"id", id}, {"name", entry.name},
            {"type", entry.type == ContentType::Theme ? "theme" : "avatar"},
            {"category", entry.category}, {"subcategory", entry.subcategory},
            {"animated", entry.animated}, {"preview_url", entry.previewUrl},
            {"download_url", entry.downloadUrl}, {"pkg_stub_url", entry.pkgStubUrl},
            {"pkg_payload_url", entry.pkgPayloadUrl}, {"file_size", entry.fileSize},
            {"stub_size", entry.stubSize}, {"payload_size", entry.payloadSize},
            {"file_format", entry.fileFormat}, {"source", entry.source},
            {"install_method", entry.installMethod}, {"tags", entry.tags},
            {"install_state", static_cast<int>(entry.installState)}, {"favorite", entry.favorite},
            {"local_path", entry.localPath}, {"local_stub_path", entry.localStubPath},
            {"local_payload_path", entry.localPayloadPath}
        });
    }
    return json{{"timestamp", manifestTimestamp_}, {"entries", items}, {"favorite_ids", favoriteIds_}}.dump();
}

// ─── Helpers ───

std::string Database::lower(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = (char)std::tolower((unsigned char)c);
    return out;
}

bool Database::contains(const std::string& haystack, const std::string& needle) {
    return lower(haystack).find(needle) != std::string::npos;
}

// ─── Queries ───

std::vector<ContentEntry*> Database::all() {
    std::vector<ContentEntry*> out;
    for (auto& [id, e] : entries_) out.push_back(&e);
    return out;
}

std::vector<ContentEntry*> Database::byType(ContentType t) {
    std::vector<ContentEntry*> out;
    for (auto& [id, e] : entries_)
        if (e.type == t) out.push_back(&e);
    return out;
}

std::vector<ContentEntry*> Database::byCategory(ContentType t, const std::string& cat) {
    std::vector<ContentEntry*> out;
    for (auto& [id, e] : entries_)
        if (e.type == t && lower(e.category) == lower(cat)) out.push_back(&e);
    return out;
}

std::vector<ContentEntry*> Database::favorites() {
    std::vector<ContentEntry*> out;
    for (auto& [id, e] : entries_)
        if (e.favorite) out.push_back(&e);
    return out;
}

std::vector<ContentEntry*> Database::installed() {
    std::vector<ContentEntry*> out;
    for (auto& [id, e] : entries_)
        if (e.installState == InstallState::Done) out.push_back(&e);
    return out;
}

std::vector<ContentEntry*> Database::search(const std::string& query,
                                              ContentType filter,
                                              bool animatedOnly) {
    std::vector<ContentEntry*> out;
    const auto normalizedQuery = lower(query);
    for (auto& [id, e] : entries_) {
        if (filter != (ContentType)255 && e.type != filter) continue;
        if (animatedOnly && !e.animated) continue;
        bool match = contains(e.name, normalizedQuery)
                  || contains(e.category, normalizedQuery)
                  || contains(e.subcategory, normalizedQuery);
        if (!match) {
            for (auto& tag : e.tags)
                if (contains(tag, normalizedQuery)) { match = true; break; }
        }
        if (match) out.push_back(&e);
    }
    // Sort by name
    std::sort(out.begin(), out.end(), [](ContentEntry* a, ContentEntry* b){
        return a->name < b->name;
    });
    return out;
}

ContentEntry* Database::byId(const std::string& id) {
    auto it = entries_.find(id);
    return it != entries_.end() ? &it->second : nullptr;
}

void Database::toggleFavorite(const std::string& id) {
    auto* e = byId(id);
    if (!e) return;
    e->favorite = !e->favorite;
    if (e->favorite) favoriteIds_.insert(id);
    else favoriteIds_.erase(id);
    ++revision_;
}

void Database::setInstallState(const std::string& id, InstallState s,
                                const std::string& localPath) {
    auto* e = byId(id);
    if (!e) return;
    e->installState = s;
    if (!localPath.empty()) e->localPath = localPath;
}

size_t Database::totalAvatars() const { return avatarCount_; }

size_t Database::totalThemes() const { return entries_.size() - avatarCount_; }

std::vector<std::string> Database::avatarCategories() {
    std::vector<std::string> cats;
    for (auto& [id, e] : entries_) {
        if (e.type != ContentType::Avatar) continue;
        if (std::find(cats.begin(), cats.end(), e.category) == cats.end())
            cats.push_back(e.category);
    }
    std::sort(cats.begin(), cats.end());
    return cats;
}

std::vector<std::string> Database::themeCategories() {
    std::vector<std::string> cats;
    for (auto& [id, e] : entries_) {
        if (e.type != ContentType::Theme) continue;
        if (std::find(cats.begin(), cats.end(), e.category) == cats.end())
            cats.push_back(e.category);
    }
    std::sort(cats.begin(), cats.end());
    return cats;
}
