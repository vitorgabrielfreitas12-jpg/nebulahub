#include "db/database.h"
#include "install/package.h"
#include "net/transfer.h"
#include "net/url.h"
#include "storage.h"
#include "ui/image.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace fs = std::filesystem;

static std::string read(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}

int main(int argc, char** argv) {
    assert(argc == 2);
    char root[] = "/tmp/nebula-tests-XXXXXX";
    assert(mkdtemp(root));
    const fs::path dir(root);
    const auto downloads = (dir / "downloads").string();
    assert(!probeDirectory(downloads + "/"));
    assert(fs::is_empty(downloads));
    assert(probeDirectory("relative").code == EINVAL);
    assert(probeDirectory((dir / "missing" / "nested").string()).code == ENOENT);
    const auto notDirectory = (dir / "file").string();
    std::ofstream(notDirectory).put('x');
    assert(probeDirectory(notDirectory).code == ENOTDIR);
    if (geteuid() != 0) {
        assert(chmod(downloads.c_str(), 0555) == 0);
        assert(probeDirectory(downloads).code == EACCES);
        assert(chmod(downloads.c_str(), 0755) == 0);
    }

    assert(fitsStorage(8192, 4096, 2) && !fitsStorage(8193, 4096, 2));
    FILE* full = fopen("/dev/full", "wb");
    assert(full);
    assert(setvbuf(full, nullptr, _IONBF, 0) == 0);
    assert(writeStorage(full, "N", 1).code == ENOSPC);
    fclose(full);

    assert(validHttpsUrl("https://archive.org/a%20b.pkg"));
    assert(!validHttpsUrl("") && !validHttpsUrl("http://example.org/x"));
    assert(!validHttpsUrl("https:///x") && !validHttpsUrl("https://user@example.org/x"));
    assert(httpsRedirect("https://a.example/a/b?x=y", "target") == "https://a.example/a/target");
    assert(httpsRedirect("https://a.example/a/b", "//b.example/x") == "https://b.example/x");
    assert(httpsRedirect("https://a.example/a/b", "http://b.example/x").empty());
    assert(transferComplete(true, false, 44, 0, 0));
    assert(transferComplete(true, false, 44, 44, 0));
    assert(!transferComplete(true, false, 43, 0, 44));
    assert(!transferComplete(false, false, 44, 44, 44));
    assert(!transferComplete(true, true, 44, 44, 44));
    assert(!transferComplete(true, false, 0, 0, 0));

    PackageInfo info;
    const fs::path build(argv[1]);
    assert(inspectPackage((dir / "not-found.pkg").string(), 0, &info) == PackageStatus::Missing);
    assert(inspectPackage(notDirectory, 0, &info) == PackageStatus::Incomplete);
    const auto pkg = read("nebulahub.pkg");
    assert(inspectPackage("nebulahub.pkg", pkg.size(), &info) == PackageStatus::Valid);
    assert(info.type == 0x1a && !isThemePayload(info));
    const auto damaged = (dir / "damaged.pkg").string();
    std::ofstream(damaged, std::ios::binary).write(pkg.data(), 8192);
    assert(inspectPackage(damaged, pkg.size()) == PackageStatus::Incomplete);
    auto invalid = pkg;
    invalid[0] = 0;
    std::ofstream(damaged, std::ios::binary).write(invalid.data(), invalid.size());
    assert(inspectPackage(damaged, pkg.size()) == PackageStatus::Invalid);
    const PackageInfo stub{"matching", 0x1c, 0}, payload{"matching", 0x1b, 2};
    assert(isThemePayload(payload) && isThemePair(stub, payload));
    assert(!isThemePair(payload, info) && !isThemePair(info, payload));

    const auto png = read(build / "extracted/uroot/assets/images/av_pack_001.png");
    SDL_Surface* decoded = decodePreview(png);
    assert(decoded && decoded->w == 256 && decoded->h == 256);
    SDL_FreeSurface(decoded);
    decoded = decodePreview(read(build / "runtime-test.jpg"));
    assert(decoded && decoded->w == 32 && decoded->h == 16);
    SDL_FreeSurface(decoded);
    assert(!decodePreview("") && !decodePreview("unsupported format"));
    assert(!decodePreview(png.substr(0, 32)));

    Database catalog;
    assert(catalog.loadManifestFile("manifest/manifest.json"));
    assert(catalog.totalAvatars() >= 4 && catalog.totalThemes() >= 850);
    catalog.toggleFavorite("av_pack_001");
    const auto existingCount = catalog.all().size();
    for (const auto* invalidJson : {"{", "{\"entries\":[]}", "{\"entries\":[{}]}",
         "{\"entries\":[{\"id\":\"x\",\"type\":\"theme\",\"name\":\"x\",\"file_size\":-1}]}",
         "{\"entries\":[{\"id\":\"x\",\"type\":\"theme\",\"name\":\"a\"},{\"id\":\"x\",\"type\":\"theme\",\"name\":\"b\"}]}"}) {
        assert(!catalog.importManifest(invalidJson));
        assert(catalog.all().size() == existingCount);
        assert(catalog.byId("av_pack_001")->favorite);
    }
    Database incoming;
    assert(incoming.importManifest(R"({"entries":[{"id":"test","name":"Test","type":"theme",
                                                     "download_url":"http://invalid.example"}]})"));
    assert(incoming.byId("test")->fileSize == 0 && incoming.byId("test")->downloadUrl.empty());
    catalog.replaceCatalog(std::move(incoming));
    assert(catalog.all().size() == 1);
    const auto saved = (dir / "saved.json").string();
    catalog.save(saved);
    Database offline;
    offline.load(saved);
    assert(offline.byId("test"));
    assert(offline.loadManifestFile("manifest/manifest.json"));
    assert(offline.byId("av_pack_001")->favorite);

    std::string large = "{\"entries\":[";
    for (int i = 0; i < 5000; ++i) {
        if (i) large += ',';
        large += "{\"id\":\"item_" + std::to_string(i) + "\",\"name\":\"Theme " +
                 std::to_string(i) + "\",\"type\":\"theme\"}";
    }
    large += "]}";
    const auto start = std::chrono::steady_clock::now();
    assert(catalog.importManifest(large));
    const auto parsed = std::chrono::steady_clock::now();
    assert(catalog.search("Theme 4999", ContentType::Theme, false).size() == 1);
    const auto searched = std::chrono::steady_clock::now();
    printf("5000 entries: parse %lld ms, search %lld us; runtime checks passed\n",
           static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(parsed - start).count()),
           static_cast<long long>(std::chrono::duration_cast<std::chrono::microseconds>(searched - parsed).count()));
    fs::remove_all(dir);
}
