#include "database.h"
#include "json.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>

using nlohmann::json;

Database::Database() = default;

void Database::load(const std::string& path) {
    dbPath_ = path;
    std::ifstream file(path);
    if (!file) return;
    std::ostringstream text;
    text << file.rdbuf();
    parseJson(text.str(), true);
}

void Database::save(const std::string& path) {
    if (path.empty()) return;
    const std::string partial = path + ".tmp";
    std::ofstream file(partial);
    file << serializeJson();
    file.close();
    if (!file || rename(partial.c_str(), path.c_str()) != 0) remove(partial.c_str());
}

void Database::importManifest(const std::string& jsonText) {
    if (!parseJson(jsonText, false)) return;
    manifestTimestamp_ = static_cast<uint64_t>(time(nullptr));
    save(dbPath_);
}

bool Database::isStale() const {
    const auto now = static_cast<uint64_t>(time(nullptr));
    return manifestTimestamp_ == 0 || now < manifestTimestamp_ ||
           now - manifestTimestamp_ > 86400;
}

bool Database::parseJson(const std::string& text, bool local) {
    const auto document = json::parse(text, nullptr, false);
    if (!document.is_object() || !document.contains("entries") ||
        !document["entries"].is_array()) return false;

    std::vector<ContentEntry> parsed;
    try {
        for (const auto& item : document["entries"]) {
            ContentEntry entry;
            entry.id = item.value("id", "");
            if (entry.id.empty() || entry.id.find_first_not_of(
                "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != std::string::npos) continue;
            entry.name = item.value("name", "");
            entry.type = item.value("type", "") == "theme" ? ContentType::Theme : ContentType::Avatar;
            entry.category = item.value("category", "");
            entry.subcategory = item.value("subcategory", "");
            entry.animated = item.value("animated", false);
            entry.previewUrl = item.value("preview_url", "");
            entry.downloadUrl = item.value("download_url", "");
            entry.pkgStubUrl = item.value("pkg_stub_url", "");
            entry.pkgPayloadUrl = item.value("pkg_payload_url", "");
            entry.fileSize = item.value<uint64_t>("file_size", 0);
            entry.stubSize = item.value<uint64_t>("stub_size", 0);
            entry.payloadSize = item.value<uint64_t>("payload_size", 0);
            entry.fileFormat = item.value("file_format", "");
            entry.source = item.value("source", "");
            entry.installMethod = item.value("install_method", "");
            entry.tags = item.value("tags", std::vector<std::string>{});
            if (local) {
                const auto state = item.value("install_state", 0);
                if (state == static_cast<int>(InstallState::Done))
                    entry.installState = InstallState::Done;
                entry.favorite = item.value("favorite", false);
                entry.localPath = item.value("local_path", "");
                entry.localStubPath = item.value("local_stub_path", "");
                entry.localPayloadPath = item.value("local_payload_path", "");
            } else if (const auto* saved = byId(entry.id)) {
                entry.installState = saved->installState;
                entry.favorite = saved->favorite;
                entry.localPath = saved->localPath;
                entry.localStubPath = saved->localStubPath;
                entry.localPayloadPath = saved->localPayloadPath;
            }
            parsed.push_back(std::move(entry));
        }
        if (local) manifestTimestamp_ = document.value<uint64_t>("timestamp", 0);
    } catch (const json::exception&) {
        return false;
    }
    for (auto& entry : parsed) {
        const auto id = entry.id;
        entries_[id] = std::move(entry);
    }
    return !parsed.empty();
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
    return json{{"timestamp", manifestTimestamp_}, {"entries", items}}.dump();
}

// ─── Helpers ───

std::string Database::lower(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = (char)std::tolower((unsigned char)c);
    return out;
}

bool Database::contains(const std::string& haystack, const std::string& needle) {
    return lower(haystack).find(lower(needle)) != std::string::npos;
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
    for (auto& [id, e] : entries_) {
        if (filter != (ContentType)255 && e.type != filter) continue;
        if (animatedOnly && !e.animated) continue;
        bool match = contains(e.name, query)
                  || contains(e.category, query)
                  || contains(e.subcategory, query);
        if (!match) {
            for (auto& tag : e.tags)
                if (contains(tag, query)) { match = true; break; }
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
    if (e) e->favorite = !e->favorite;
}

void Database::setInstallState(const std::string& id, InstallState s,
                                const std::string& localPath) {
    auto* e = byId(id);
    if (!e) return;
    e->installState = s;
    if (!localPath.empty()) e->localPath = localPath;
}

size_t Database::totalAvatars() const {
    size_t n = 0;
    for (auto& [id, e] : entries_) if (e.type == ContentType::Avatar) ++n;
    return n;
}

size_t Database::totalThemes() const {
    size_t n = 0;
    for (auto& [id, e] : entries_) if (e.type == ContentType::Theme) ++n;
    return n;
}

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
