#include "db/database.h"
#include "install/package.h"
#include "net/transfer.h"
#include "net/fetcher.h"
#include "net/url.h"
#include "storage.h"
#include "ui/image.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <future>

namespace fs = std::filesystem;

static std::string read(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}

int main(int argc, char** argv) {
    assert(argc == 2 || argc == 3);
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
    assert(diagnosticOrigin("https://archive.org/private/secret?token=value") == "https://archive.org");
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

    auto invalidOffsets = pkg;
    invalidOffsets[0x18] = char(0xff);
    std::ofstream(damaged, std::ios::binary).write(invalidOffsets.data(), invalidOffsets.size());
    assert(std::string(inspectPackageDetails(damaged, 0).reason) == "Invalid PKG entry table");
    std::ofstream(damaged, std::ios::binary).write(pkg.data(), 32);
    assert(std::string(inspectPackageDetails(damaged, 0).reason) == "Truncated PKG header");
    std::ofstream(damaged, std::ios::binary) << "<html>content unavailable</html>";
    assert(std::string(inspectPackageDetails(damaged, 0).reason) == "Invalid PKG magic");
    assert(isCompletePackage("nebulahub.pkg", 0));
    assert(matchesPackageRole(info, PackageRole::AvatarPack));
    assert(matchesPackageRole(stub, PackageRole::ThemeFirst));
    assert(matchesPackageRole(payload, PackageRole::ThemeFirst));
    assert(!matchesPackageRole(stub, PackageRole::ThemePayload));
    assert(matchesPackageRole(payload, PackageRole::ThemePayload));
    assert(!matchesPackageRole(payload, PackageRole::AvatarPack));

    // Real sparse file above 4 GiB. Its metadata and PFS occupy separate
    // ranges, as observed in the actual source packages.
    auto largeHeader = pkg.substr(0, 0x438);
    const uint64_t sparseSize = (uint64_t{1} << 32) + (uint64_t{1} << 20);
    const auto be64 = [&](size_t pos, uint64_t value) {
        for (int i = 7; i >= 0; --i) {
            largeHeader[pos + i] = static_cast<char>(value & 0xff);
            value >>= 8;
        }
    };
    constexpr uint64_t pfsOffset = 0x80000;
    be64(0x418, sparseSize - pfsOffset);
    be64(0x430, 0); // retail variant with a verifiable EOF from the PFS range
    const auto sparse = (dir / "large.pkg").string();
    std::ofstream(sparse, std::ios::binary).write(largeHeader.data(), largeHeader.size());
    fs::resize_file(sparse, sparseSize);
    const auto inspected = inspectPackageDetails(sparse, sparseSize);
    assert(inspected && inspected.fileSize == sparseSize && inspected.declaredSize == 0);
    fs::resize_file(sparse, sparseSize - 1);
    assert(inspectPackage(sparse, 0) == PackageStatus::Invalid);

    std::vector<DownloadJob> queue(2);
    queue[0].id = "first";
    queue[1].id = "second";
    assert(beginNextDownload(queue).id == "first" && queue[0].state == DownloadState::Connecting);
    queue[0].state = DownloadState::Validating;
    assert(beginNextDownload(queue).id.empty() && queue[1].state == DownloadState::Queued);
    queue[0].state = DownloadState::Completed;
    const auto published = fs::path(downloads) / "first.pkg";
    std::ofstream(published, std::ios::binary).write(pkg.data(), pkg.size());
    assert(beginNextDownload(queue).id == "second");
    cancelOrDismiss(queue, "first");
    assert(fs::exists(published) && queue.size() == 1);
    cancelOrDismiss(queue, "second");
    assert(queue.front().state == DownloadState::Cancelled);
    queue.front().state = DownloadState::Queued;
    assert(beginNextDownload(queue).id == "second");

    const auto pending = fs::path(downloads) / "interrupted.pkg.part-X7m10a";
    std::ofstream(pending) << "partial data";
    assert(cleanupInterruptedDownloads(downloads) == 1 && !fs::exists(pending));
    assert(fs::exists(published));
    const auto rejectedPath = (dir / "diagnostic-rejected.bin").string();
    RejectedDownload rejected(rejectedPath);
    fs::resize_file(sparse, RejectedDownload::limit + 1);
    assert(!rejected.keep(sparse) && rejected.size() == 0);
    assert(rejected.keep(damaged) && !fs::exists(damaged));
    RejectedDownload reopened(rejectedPath);
    assert(reopened.size() > 0 && reopened.clear() && reopened.size() == 0);

    const auto packaged = build / "extracted/uroot/assets/images";
    auto worker = std::async(std::launch::async, [&] {
        size_t checked = 0;
        for (const auto& file : fs::directory_iterator(packaged)) {
            auto* surface = loadPreviewFile(file.path().string());
            assert(surface && surface->w == 256 && surface->h == 256);
            SDL_FreeSurface(surface);
            ++checked;
        }
        return checked;
    });
    assert(worker.get() == 6);
    const auto png = read(packaged / "av_pack_001.png");
    SDL_Surface* decoded = loadPreviewFile((build / "runtime-test.jpg").string());
    assert(decoded && decoded->w == 32 && decoded->h == 16);
    SDL_FreeSurface(decoded);
    decoded = loadPreviewFile((build / "runtime-alpha.png").string());
    assert(decoded && decoded->w == 16 && static_cast<unsigned char*>(decoded->pixels)[3] == 0);
    SDL_FreeSurface(decoded);
    const auto disguised = dir / "mislabelled.jpeg";
    std::ofstream(disguised, std::ios::binary).write(png.data(), png.size());
    decoded = loadPreviewFile(disguised.string());
    assert(decoded && decoded->w == 256);
    SDL_FreeSurface(decoded);
    PreviewError previewError;
    assert(!loadPreviewFile((dir / "missing.png").string(), &previewError) && previewError.ioError == ENOENT);
    assert(!loadPreviewFile(downloads, &previewError));
    assert(!decodePreview("") && !decodePreview("unsupported format") && !decodePreview(png.substr(0, 32)));

    if (argc == 3) {
        const fs::path sources(argv[2]);
        const auto anime = inspectPackageDetails((sources / "anime").string(), 157810688);
        const auto azure1 = inspectPackageDetails((sources / "azure_1").string(), 1048576);
        const auto azure2 = inspectPackageDetails((sources / "azure_2").string(), 13369344);
        assert(anime && matchesPackageRole(anime.info, PackageRole::AvatarPack));
        assert(azure1 && azure2 && isThemePair(azure1.info, azure2.info));
        unsigned images = 0;
        for (const auto& file : fs::directory_iterator(sources)) {
            if (file.path().filename().string().rfind("preview_", 0) != 0 ||
                file.path().extension() == ".headers") continue;
            auto* image = loadPreviewFile(file.path().string());
            assert(image && image->w == 1920 && image->h == 1080);
            SDL_FreeSurface(image);
            ++images;
        }
        assert(images == 3);
        if (fs::exists(sources / "static_1") && fs::exists(sources / "static_2")) {
            const auto static1 = inspectPackageDetails((sources / "static_1").string(), 131072);
            const auto static2 = inspectPackageDetails((sources / "static_2").string(), 10616832);
            assert(static1 && static2 && isThemePair(static1.info, static2.info));
            puts("Both Dark Souls III static theme components accepted");
        }
        puts("Anime Pack, both Azure Earth components and all three live JPEG sources accepted");
    }

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
