#include "db/database.h"
#include "install/package.h"
#include "install/prepare.h"
#include "install/theme_progress.h"
#include "net/transfer.h"
#include "net/fetcher.h"
#include "net/url.h"
#include "storage.h"
#include "ui/image.h"
#include "ui/preview_texture.h"

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

namespace {
struct MockInstaller {
    static MockInstaller* current;
    int32_t loadCode = 0;
    int32_t initCode = 0;
    int32_t submitCode = 0;
    int32_t terminateCode = 0;
    InstallSymbol override{};
    std::string symbolOverride;
    std::vector<std::string> operations;
    std::string path;
    int sends = 0;
    bool interrupted = false;

    static int32_t begin() {
        current->operations.emplace_back("initialize");
        return current->initCode;
    }
    static int32_t submit(const char* path, void* options) {
        assert(path && !options);
        current->operations.emplace_back("submit");
        current->path = path;
        ++current->sends;
        if (current->interrupted) throw std::runtime_error("interrupted");
        return current->submitCode;
    }
    static int32_t finish() {
        current->operations.emplace_back("terminate");
        return current->terminateCode;
    }
    InstallService service() {
        current = this;
        return {
            [this] { operations.emplace_back("load"); return loadCode; },
            [this](const char* name) {
                if (name == symbolOverride) return override;
                const std::string symbol(name);
                if (symbol == "sceAppInstUtilInitialize") return InstallSymbol{reinterpret_cast<void*>(begin), 0, true};
                if (symbol == "sceAppInstUtilTerminate") return InstallSymbol{reinterpret_cast<void*>(finish), 0, true};
                assert(symbol == "sceAppInstUtilAppInstallPkg");
                return InstallSymbol{reinterpret_cast<void*>(submit), 0, true};
            }
        };
    }
};
MockInstaller* MockInstaller::current = nullptr;

void checkInstaller(const fs::path& dir, const fs::path& downloaded, const std::string& bytes) {
    const InstallTrace quiet = [](const char*, bool, int32_t) {};
    const auto staged = (dir / "first.pkg").string();
    assert(!stagePackage(downloaded.string(), staged, quiet));
    assert(read(staged) == bytes && read(downloaded) == bytes);
    assert(isCompletePackage(staged, bytes.size()));
    assert(stagePackage((dir / "missing.pkg").string(), staged, quiet).code == ENOENT);
    const auto failure = stagePackage(downloaded.string(), (dir / "downloads").string(), quiet);
    assert(failure && std::string(failure.operation) == "publish staged package");
    for (const auto& file : fs::directory_iterator(dir))
        assert(file.path().filename().string().rfind("downloads.", 0) != 0);
    assert(read(staged) == bytes && read(downloaded) == bytes);

    MockInstaller mock;
    const auto send = [&](const std::function<bool()>& before) {
        return requestPackage(mock.service(), staged.c_str(), before, quiet);
    };
    const auto allow = [] { return true; };
    mock.loadCode = -12;
    assert(send(allow).code == -12 && mock.operations == std::vector<std::string>{"load"});
    mock.loadCode = 0;
    mock.symbolOverride = "sceAppInstUtilAppInstallPkg";
    mock.override = {nullptr, -7, false};
    assert(send(allow).code == -7 && mock.sends == 0);
    mock.override = {nullptr, 0, true};
    assert(send(allow).failure && mock.sends == 0);
    mock.override = {reinterpret_cast<void*>(MockInstaller::submit), 0, false};
    assert(send(allow).failure && mock.sends == 0);
    mock.symbolOverride.clear();

    mock.initCode = -31;
    mock.operations.clear();
    assert(send(allow).code == -31 && mock.sends == 0);
    assert((mock.operations == std::vector<std::string>{"load", "initialize"}));
    mock.initCode = 0;
    mock.operations.clear();
    assert(!send([] { return false; }).accepted && mock.sends == 0);
    assert((mock.operations == std::vector<std::string>{"load", "initialize", "terminate"}));

    const auto state = (dir / "install.state").string();
    const std::string key = "azure:contentid";
    ThemeProgress progress(state);
    assert(progress.next(key, false) == ThemeAction::First);
    mock.submitCode = -44;
    const auto rejected = requestThemePart(progress, key, false, send, quiet);
    assert(!rejected.accepted && rejected.submitReturned && rejected.code == -44 && mock.sends == 1);
    ThemeProgress uncertain(state);
    assert(uncertain.phase == ThemePhase::FirstRequested);
    assert(uncertain.next(key, false) == ThemeAction::ConfirmFirst);
    assert(uncertain.clear() == 0 && !fs::exists(state));
    assert(fs::exists(downloaded) && read(downloaded) == bytes);

    mock.submitCode = 0;
    mock.terminateCode = -101;
    assert(requestThemePart(progress, key, false, send, quiet).accepted && mock.sends == 2);
    ThemeProgress restarted(state);
    assert(restarted.phase == ThemePhase::FirstAccepted);
    assert(restarted.next(key, false) == ThemeAction::ConfirmFirst);
    assert(restarted.next("japanese-trade:contentid", true) == ThemeAction::OtherTheme);
    assert(mock.sends == 2);
    if (restarted.next(key, true) == ThemeAction::Second) {
        const auto secondPath = (dir / "second.pkg").string();
        assert(!stagePackage(downloaded.string(), secondPath, quiet));
        const auto sendSecond = [&](const std::function<bool()>& before) {
            return requestPackage(mock.service(), secondPath.c_str(), before, quiet);
        };
        assert(requestThemePart(restarted, key, true, sendSecond, quiet).accepted);
        assert(mock.path == secondPath && mock.operations.back() == "terminate");
    }
    assert(mock.sends == 3 && ThemeProgress(state).next(key, true) == ThemeAction::CheckSecond);
    assert(restarted.clear() == 0);

    mock.interrupted = true;
    try {
        requestThemePart(restarted, key, false, send, quiet);
        assert(false);
    } catch (const std::runtime_error&) {
        assert(mock.operations.back() == "terminate");
    }
    assert(ThemeProgress(state).phase == ThemePhase::FirstRequested);
    assert(ThemeProgress(state).next(key, false) == ThemeAction::ConfirmFirst);
    std::ofstream(state) << "corrupt";
    assert(ThemeProgress(state).next(key, true) == ThemeAction::Reset);
    assert(ThemeProgress(state).clear() == 0);
}

int rejectRenderCopy(SDL_Renderer*, SDL_Texture*, const SDL_Rect*, const SDL_Rect*) {
    return SDL_SetError("simulated render failure");
}

void checkPreviewTextures(const fs::path& dir, const fs::path& hdJpeg) {
    using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)>;
    auto worker = std::async(std::launch::async, [&] {
        Surface decoded(loadPreviewFile(hdJpeg.string()), SDL_FreeSurface);
        assert(decoded && decoded->w == 1920 && decoded->h == 1080);
        return Surface(downsamplePreview(*decoded), SDL_FreeSurface);
    });
    const auto preview = worker.get();
    assert(preview && preview->w == 768 && preview->h == 432);
    const auto path = dir / "disk-cache.img";
    std::ofstream(path, std::ios::binary) << read(hdJpeg);
    Surface cached(loadPreviewFile(path.string()), SDL_FreeSurface);
    assert(cached && cached->w == 1920);
    std::ofstream(path, std::ios::binary) << "corrupted";
    assert(!loadPreviewFile(path.string()));
    assert(SDL_Init(0) == 0);
    Surface target(SDL_CreateRGBSurfaceWithFormat(0, 640, 640, 32, SDL_PIXELFORMAT_RGBA32), SDL_FreeSurface);
    assert(target);
    std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(
        SDL_CreateSoftwareRenderer(target.get()), SDL_DestroyRenderer);
    assert(renderer);
    {
        PreviewTextures textures(renderer.get());
        PreviewTextures unavailable(nullptr);
        PreviewTextures cannotDraw(renderer.get(), rejectRenderCopy);
        const SDL_Rect square{0, 0, 640, 640};
        assert(SDL_SetRenderDrawColor(renderer.get(), 0, 0, 0, 255) == 0);
        assert(SDL_RenderClear(renderer.get()) == 0);
        assert(!textures.draw("no-preview", "file", nullptr, square));
        assert(!unavailable.draw("no-renderer", "file", preview.get(), square));
        assert(!cannotDraw.draw("cannot-draw", "file", preview.get(), square));
        assert(textures.draw("theme", "file", preview.get(), square));
        assert(textures.draw("theme", "file", nullptr, square)); // Cache hit.
        assert(!textures.draw("theme", "different-file", nullptr, square));
        SDL_RenderPresent(renderer.get());
        const auto* pixels = static_cast<const unsigned char*>(target->pixels);
        assert(pixels[0] == 0 && pixels[1] == 0 && pixels[2] == 0);
        assert(pixels[target->pitch * 320 + 320 * 4] != 0);
        for (int n = 0; n < 13; ++n)
            assert(textures.draw(std::to_string(n), "file", preview.get(), square));
        assert(!textures.draw("theme", "file", nullptr, square)); // Evicted.
    }
    renderer.reset();
    SDL_Quit();
}
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
    checkInstaller(dir, published, pkg);
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
    assert(png == read(packaged / "av_pack_002.png") && png == read(packaged / "av_pack_003.png"));
    const auto themeIcon = read(packaged / "th_horizon_zd_dyn.png");
    assert(themeIcon != png && themeIcon == read(packaged / "th_ghost_tsushima_dyn.png") &&
           themeIcon == read(packaged / "th_dark_souls_3_static.png"));
    checkPreviewTextures(dir, build / "runtime-hd.jpg");
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
            auto* prepared = downsamplePreview(*image);
            assert(prepared && prepared->w == 768 && prepared->h == 432);
            SDL_FreeSurface(prepared);
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
        if (fs::exists(sources / "japanese_1") && fs::exists(sources / "japanese_2")) {
            const auto japanese1 = inspectPackageDetails((sources / "japanese_1").string(), 1048576);
            const auto japanese2 = inspectPackageDetails((sources / "japanese_2").string(), 19922944);
            assert(japanese1 && japanese2 && isThemePair(japanese1.info, japanese2.info));
            puts("Both 10 Images — 1800s Japanese Trade components accepted");
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
