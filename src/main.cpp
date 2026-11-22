// NEBULA HUB — Main Entry Point
// PS4 Homebrew | OpenOrbis Toolchain | SDL2

#include "nebula.h"
#include "ui/renderer.h"
#include "ui/screens.h"
#include "net/fetcher.h"
#include "db/database.h"
#include "input/controller.h"
#include "install/installer.h"

#include <orbis/libkernel.h>
#include <orbis/UserService.h>
#include <orbis/Sysmodule.h>
#include <orbis/Pad.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL_ttf.h>

#include <cstdio>
#include <fstream>
#include <sstream>

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
    IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG);
    if (sceSysmoduleLoadModule(ORBIS_SYSMODULE_FREETYPE_OL) < 0 || TTF_Init() < 0) {
        fprintf(stderr, "FreeType initialization failed: %s\n", SDL_GetError());
        IMG_Quit();
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
        IMG_Quit();
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
        const bool refreshManifest = db.isStale();
        if (db.all().empty()) {
            std::ifstream manifest("/app0/assets/misc/manifest.json");
            std::ostringstream text;
            text << manifest.rdbuf();
            db.importManifest(text.str());
        }
        if (refreshManifest) net.fetchManifestAsync(MANIFEST_URL);

        // UI state machine
        ScreenManager screens(gfx, db, net, ctrl, installer);
        screens.push(ScreenID::Home);

        // Main loop
        bool running = true;
        while (running) {

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
                const auto manifest = net.takeManifest();
                if (!manifest.empty()) db.importManifest(manifest);
            }
            screens.update();
            gfx.clear();
            screens.render();
            gfx.present();
            SDL_UpdateWindowSurface(window);
            SDL_Delay(16);
        }

        // Cleanup
        db.save(dbPath);
    }
    TTF_Quit();
    IMG_Quit();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
