// NEBULA HUB — Main Entry Point
// PS4 Homebrew | OpenOrbis Toolchain | SDL2

#include "nebula.h"
#include "diagnostics.h"
#include "ui/renderer.h"
#include "ui/screens.h"
#include "net/fetcher.h"
#include "net/url.h"
#include "db/database.h"
#include "input/controller.h"
#include "install/installer.h"

#include <orbis/libkernel.h>
#include <orbis/UserService.h>
#include <orbis/Sysmodule.h>
#include <orbis/Pad.h>

#include <SDL2/SDL.h>
#include <SDL_ttf.h>

#include <cstdio>
#include <fstream>

// ─────────────────────────────────────────────
//  Boot
// ─────────────────────────────────────────────

int main() {

    // PS4 service init
    OrbisUserServiceInitializeParams userParams{};
    userParams.priority = ORBIS_KERNEL_PRIO_FIFO_LOWEST;
    sceUserServiceInitialize(&userParams);
    if (scePadInit() < 0) return 1;

    OrbisUserServiceUserId userId = 0;
    if (sceUserServiceGetInitialUser(&userId) < 0) return 1;

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    if (sceSysmoduleLoadModule(ORBIS_SYSMODULE_FREETYPE_OL) < 0 || TTF_Init() < 0) {
        fprintf(stderr, "FreeType initialization failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(APP_TITLE, SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED, SCREEN_W, SCREEN_H, 0);
    SDL_Surface* surface = window ? SDL_GetWindowSurface(window) : nullptr;
    SDL_Renderer* renderer = surface ? SDL_CreateSoftwareRenderer(surface) : nullptr;
    if (!renderer) {
        fprintf(stderr, "SDL renderer initialization failed: %s\n", SDL_GetError());
        if (window) SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    {
        Renderer gfx(renderer);
        Database db;
        Fetcher net;
        Controller ctrl(userId);
        Installer installer;

        const std::string dbPath = std::string(DATA_DIR) + "db.json";
        db.load(dbPath);
        std::ifstream config(DATA_DIR "manifest-url.txt");
        char configured[2050]{};
        std::string manifestUrl = MANIFEST_URL;
        std::string configError;
        if (config) {
            config.getline(configured, sizeof(configured));
            manifestUrl = configured;
            if (!manifestUrl.empty() && manifestUrl.back() == '\r') manifestUrl.pop_back();
            if (!config || (!manifestUrl.empty() && !validHttpsUrl(manifestUrl))) {
                configError = diagnostic("catalog", "Invalid configured HTTPS URL", ErrorDomain::Application);
                manifestUrl.clear();
            }
        }
        if (manifestUrl.empty() || !db.loadManifestFile(DATA_DIR "manifest-cache.json")) {
            if (!db.loadManifestFile("/app0/assets/misc/manifest.json"))
                diagnostic("catalog", "Cannot read bundled catalog", ErrorDomain::Application);
        }
        if (!manifestUrl.empty()) net.fetchManifestAsync(manifestUrl);

        // UI state machine
        ScreenManager screens(gfx, db, net, ctrl, installer, !manifestUrl.empty(), configError);
        screens.push(ScreenID::Home);

        // Main loop
        bool running = true;
        bool reportedPresentationError = false;
        unsigned slowFrames = 0;
        while (running) {
            const auto frameStart = sceKernelGetProcessTime();

            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                if (ev.type == SDL_QUIT) running = false;
            }

            ctrl.poll();

            if (ctrl.pressed(ORBIS_PAD_BUTTON_OPTIONS)) {
                if (screens.depth() == 1) running = false;
                else screens.pop();
            } else if (screens.depth() > 1 && ctrl.pressed(ORBIS_PAD_BUTTON_CIRCLE)) {
                screens.pop();
            }

            if (screens.depth() == 1) {
                if (auto manifest = net.takeManifest()) db.replaceCatalog(std::move(*manifest));
            }
            screens.update();
            gfx.clear();
            screens.render();
            gfx.present();
            if (SDL_UpdateWindowSurface(window) < 0 && !reportedPresentationError) {
                diagnostic("video", "Present window surface", ErrorDomain::Application, EIO, SDL_GetError());
                reportedPresentationError = true;
            }
            const auto elapsed = sceKernelGetProcessTime() - frameStart;
            if (elapsed > 33333 && ++slowFrames == 120) {
                diagnostic("video", "120 frames exceeded 33ms", ErrorDomain::Event, 0,
                           ("latest_us=" + std::to_string(elapsed)).c_str());
            }
            if (elapsed < 16667) sceKernelUsleep(16667 - elapsed);
        }

        // Cleanup
        db.save(dbPath);
    }
    TTF_Quit();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
