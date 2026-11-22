// NEBULA HUB — Database implementation
// Uses a minimal hand-rolled JSON parser (no deps beyond libc)
// for PS4 compatibility — no Boost, no nlohmann on this platform.

#include "database.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <ctime>
#include <orbis/libkernel.h>

// ─────────────────────────────────────────────
//  Minimal JSON value extraction helpers
// ─────────────────────────────────────────────

static std::string jsonString(const std::string& src, const std::string& key) {
    std::string k = "\"" + key + "\":\"";
    auto pos = src.find(k);
    if (pos == std::string::npos) return "";
    pos += k.size();
    auto end = src.find('"', pos);
    if (end == std::string::npos) return "";
    return src.substr(pos, end - pos);
}

static uint64_t jsonUint(const std::string& src, const std::string& key) {
    std::string k = "\"" + key + "\":";
    auto pos = src.find(k);
    if (pos == std::string::npos) return 0;
    pos += k.size();
    while (pos < src.size() && src[pos] == ' ') ++pos;
    uint64_t v = 0;
    while (pos < src.size() && std::isdigit((unsigned char)src[pos]))
        v = v * 10 + (src[pos++] - '0');
    return v;
}

static bool jsonBool(const std::string& src, const std::string& key) {
    std::string k = "\"" + key + "\":";
    auto pos = src.find(k);
    if (pos == std::string::npos) return false;
    pos += k.size();
    while (pos < src.size() && src[pos] == ' ') ++pos;
    return src.substr(pos, 4) == "true";
}

static std::vector<std::string> jsonStringArray(const std::string& src, const std::string& key) {
    std::vector<std::string> out;
    std::string k = "\"" + key + "\":[";
    auto pos = src.find(k);
    if (pos == std::string::npos) return out;
    pos += k.size();
    auto end = src.find(']', pos);
    if (end == std::string::npos) return out;
    std::string arr = src.substr(pos, end - pos);
    size_t p = 0;
    while ((p = arr.find('"', p)) != std::string::npos) {
        ++p;
        auto q = arr.find('"', p);
        if (q == std::string::npos) break;
        out.push_back(arr.substr(p, q - p));
        p = q + 1;
    }
    return out;
}

// Split a JSON array of objects by their opening brace
static std::vector<std::string> splitObjects(const std::string& src) {
    std::vector<std::string> objs;
    int depth = 0;
    size_t start = std::string::npos;
    for (size_t i = 0; i < src.size(); ++i) {
        if (src[i] == '{') {
            if (depth == 0) start = i;
            ++depth;
        } else if (src[i] == '}') {
            --depth;
            if (depth == 0 && start != std::string::npos)
                objs.push_back(src.substr(start, i - start + 1));
        }
    }
    return objs;
}

// ─────────────────────────────────────────────
//  Database impl
// ─────────────────────────────────────────────

Database::Database() {}

void Database::load(const std::string& path) {
    dbPath_ = path;
    std::ifstream f(path);
    if (!f) return;
    std::ostringstream ss;
    ss << f.rdbuf();
    parseJson(ss.str());
}

void Database::save(const std::string& path) {
    std::ofstream f(path);
    if (f) f << serializeJson();
}

void Database::importManifest(const std::string& jsonText) {
    // Merge new entries, preserve local install state + favorites
    std::unordered_map<std::string, ContentEntry> existing = entries_;
    parseJson(jsonText);
    for (auto& [id, e] : entries_) {
        if (existing.count(id)) {
            e.installState  = existing[id].installState;
            e.favorite      = existing[id].favorite;
            e.localPath     = existing[id].localPath;
        }
    }
    manifestTimestamp_ = (uint64_t)time(nullptr);
    save(dbPath_);
}

bool Database::isStale() const {
    if (manifestTimestamp_ == 0) return true;
    uint64_t now = (uint64_t)time(nullptr);
    return (now - manifestTimestamp_) > 86400; // 24 h
}

void Database::parseJson(const std::string& json) {
    // Find "entries" array
    auto pos = json.find("\"entries\":[");
    if (pos == std::string::npos) return;
    pos += 11;
    auto end = json.rfind(']');
    if (end == std::string::npos) return;
    std::string arr = json.substr(pos, end - pos);
    auto objs = splitObjects(arr);
    for (auto& obj : objs) {
        ContentEntry e;
        e.id            = jsonString(obj, "id");
        if (e.id.empty()) continue;
        e.name          = jsonString(obj, "name");
        std::string t   = jsonString(obj, "type");
        e.type          = (t == "theme") ? ContentType::Theme : ContentType::Avatar;
        e.category      = jsonString(obj, "category");
        e.subcategory   = jsonString(obj, "subcategory");
        e.animated      = jsonBool(obj, "animated");
        e.previewUrl    = jsonString(obj, "preview_url");
        e.downloadUrl   = jsonString(obj, "download_url");
        e.pkgStubUrl    = jsonString(obj, "pkg_stub_url");
        e.pkgPayloadUrl = jsonString(obj, "pkg_payload_url");
        e.fileSize      = jsonUint(obj, "file_size");
        e.stubSize      = jsonUint(obj, "stub_size");
        e.payloadSize   = jsonUint(obj, "payload_size");
        e.fileFormat    = jsonString(obj, "file_format");
        e.source        = jsonString(obj, "source");
        e.installMethod = jsonString(obj, "install_method");
        e.tags          = jsonStringArray(obj, "tags");
        entries_[e.id]  = e;
    }
}

std::string Database::serializeJson() const {
    // Write compact JSON for persistence
    std::ostringstream o;
    o << "{\"timestamp\":" << manifestTimestamp_ << ",\"entries\":[\n";
    bool first = true;
    for (auto& [id, e] : entries_) {
        if (!first) o << ",\n";
        first = false;
        o << "{\"id\":\"" << e.id << "\","
          << "\"name\":\"" << e.name << "\","
          << "\"type\":\"" << (e.type==ContentType::Theme?"theme":"avatar") << "\","
          << "\"category\":\"" << e.category << "\","
          << "\"subcategory\":\"" << e.subcategory << "\","
          << "\"animated\":" << (e.animated?"true":"false") << ","
          << "\"preview_url\":\"" << e.previewUrl << "\","
          << "\"download_url\":\"" << e.downloadUrl << "\","
          << "\"pkg_stub_url\":\"" << e.pkgStubUrl << "\","
          << "\"pkg_payload_url\":\"" << e.pkgPayloadUrl << "\","
          << "\"file_size\":" << e.fileSize << ","
          << "\"stub_size\":" << e.stubSize << ","
          << "\"payload_size\":" << e.payloadSize << ","
          << "\"file_format\":\"" << e.fileFormat << "\","
          << "\"source\":\"" << e.source << "\","
          << "\"install_method\":\"" << e.installMethod << "\","
          << "\"install_state\":" << (int)e.installState << ","
          << "\"favorite\":" << (e.favorite?"true":"false") << ","
          << "\"local_path\":\"" << e.localPath << "\","
          << "\"tags\":[";
        for (size_t i = 0; i < e.tags.size(); ++i) {
            if (i) o << ",";
            o << "\"" << e.tags[i] << "\"";
        }
        o << "]}";
    }
    o << "\n]}";
    return o.str();
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
