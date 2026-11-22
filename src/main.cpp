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
#include <orbis/SystemService.h>
#include <orbis/Pad.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>

#include <cstdio>
#include <cstring>

// ─────────────────────────────────────────────
//  Boot
// ─────────────────────────────────────────────

int main() {

    // PS4 service init
    sceUserServiceInitialize(nullptr);
    scePadInit();

    OrbisUserServiceUserId userId = 0;
    if (sceUserServiceGetInitialUser(&userId) < 0) return 1;

    // SDL init
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
    IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG);
    TTF_Init();

    SDL_Window* window = SDL_CreateWindow(
        "NEBULA HUB",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        SCREEN_W, SCREEN_H,
        SDL_WINDOW_FULLSCREEN
    );

    SDL_Renderer* renderer = SDL_CreateRenderer(
        window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );

    SDL_RenderSetLogicalSize(renderer, SCREEN_W, SCREEN_H);

    // Core systems
    Renderer  gfx(renderer);
    Database  db;
    Fetcher   net;
    Controller ctrl(userId);
    Installer installer(userId);

    db.load("/data/NEBULAHUB/db.json");
    net.setManifestUrl(MANIFEST_URL);

    // Fetch manifest in background if stale
    if (db.isStale()) {
        net.fetchManifestAsync([&](bool ok) {
            if (ok) db.importManifest(net.getManifest());
        });
    }

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

        if (ctrl.pressed(PAD_BUTTON_OPTIONS)) {
            if (screens.depth() == 1) running = false;
            else screens.pop();
        }

        screens.update();
        gfx.clear();
        screens.render();
        gfx.present();
    }

    // Cleanup
    db.save("/data/NEBULAHUB/db.json");
    TTF_Quit();
    IMG_Quit();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
