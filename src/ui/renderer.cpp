// NEBULA HUB — Renderer (SDL2 wrapper)

#include "renderer.h"
#include <cstring>
#include <cmath>
#include <algorithm>

Renderer::Renderer(SDL_Renderer* r) : sdl_(r) {}

Renderer::~Renderer() {
    for (auto& [k, f] : fonts_) TTF_CloseFont(f);
    for (auto& [k, t] : texCache_) SDL_DestroyTexture(t);
}

TTF_Font* Renderer::getFont(int size, bool bold) {
    std::string key = fontKey(size, bold);
    auto it = fonts_.find(key);
    if (it != fonts_.end()) return it->second;
    const char* path = bold
        ? FONT_BOLD
        : FONT_REGULAR;
    TTF_Font* f = TTF_OpenFont(path, size);
    if (!f) {
        // Fallback: try next size or return nullptr
        return nullptr;
    }
    fonts_[key] = f;
    return f;
}

void Renderer::clear(Color bg) {
    SDL_SetRenderDrawColor(sdl_, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderClear(sdl_);
}

void Renderer::present() { SDL_RenderPresent(sdl_); }

void Renderer::fillRect(int x, int y, int w, int h, Color c, float alpha) {
    SDL_SetRenderDrawBlendMode(sdl_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sdl_, c.r, c.g, c.b, (uint8_t)(c.a * alpha));
    SDL_Rect r{x, y, w, h};
    SDL_RenderFillRect(sdl_, &r);
}

void Renderer::drawRect(int x, int y, int w, int h, Color c, int thick) {
    SDL_SetRenderDrawColor(sdl_, c.r, c.g, c.b, c.a);
    for (int t = 0; t < thick; ++t) {
        SDL_Rect r{x+t, y+t, w-2*t, h-2*t};
        SDL_RenderDrawRect(sdl_, &r);
    }
}

void Renderer::fillRoundRect(int x, int y, int w, int h, int radius, Color c, float alpha) {
    // Simple approximation: filled rect + four corner fills
    uint8_t a = (uint8_t)(c.a * alpha);
    SDL_SetRenderDrawBlendMode(sdl_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sdl_, c.r, c.g, c.b, a);
    // Center rect (no corners)
    SDL_Rect center{x + radius, y, w - 2*radius, h};
    SDL_RenderFillRect(sdl_, &center);
    SDL_Rect left{x, y + radius, radius, h - 2*radius};
    SDL_RenderFillRect(sdl_, &left);
    SDL_Rect right{x + w - radius, y + radius, radius, h - 2*radius};
    SDL_RenderFillRect(sdl_, &right);
    // Fill corners with circles (pixel-level, small radius)
    for (int dy = 0; dy < radius; ++dy) {
        for (int dx = 0; dx < radius; ++dx) {
            if (dx*dx + dy*dy <= radius*radius) {
                // Top-left
                SDL_RenderDrawPoint(sdl_, x + radius - 1 - dx, y + radius - 1 - dy);
                // Top-right
                SDL_RenderDrawPoint(sdl_, x + w - radius + dx, y + radius - 1 - dy);
                // Bottom-left
                SDL_RenderDrawPoint(sdl_, x + radius - 1 - dx, y + h - radius + dy);
                // Bottom-right
                SDL_RenderDrawPoint(sdl_, x + w - radius + dx, y + h - radius + dy);
            }
        }
    }
}

void Renderer::drawLine(int x1, int y1, int x2, int y2, Color c) {
    SDL_SetRenderDrawColor(sdl_, c.r, c.g, c.b, c.a);
    SDL_RenderDrawLine(sdl_, x1, y1, x2, y2);
}

void Renderer::gradientH(int x, int y, int w, int h, Color left, Color right) {
    for (int i = 0; i < w; ++i) {
        float t = (float)i / (float)(w - 1);
        Color c {
            (uint8_t)(left.r + t*(right.r - left.r)),
            (uint8_t)(left.g + t*(right.g - left.g)),
            (uint8_t)(left.b + t*(right.b - left.b)),
            (uint8_t)(left.a + t*(right.a - left.a))
        };
        SDL_SetRenderDrawColor(sdl_, c.r, c.g, c.b, c.a);
        SDL_RenderDrawLine(sdl_, x+i, y, x+i, y+h);
    }
}

void Renderer::text(const std::string& str, int x, int y, Color c,
                    int size, bool bold, bool shadow) {
    if (str.empty()) return;
    TTF_Font* f = getFont(size, bold);
    if (!f) return;
    if (shadow) {
        SDL_Color sc{0, 0, 0, 160};
        SDL_Surface* ss = TTF_RenderUTF8_Blended(f, str.c_str(), sc);
        if (ss) {
            SDL_Texture* st = SDL_CreateTextureFromSurface(sdl_, ss);
            SDL_Rect dr{x+2, y+2, ss->w, ss->h};
            SDL_RenderCopy(sdl_, st, nullptr, &dr);
            SDL_DestroyTexture(st);
            SDL_FreeSurface(ss);
        }
    }
    SDL_Color sc{c.r, c.g, c.b, c.a};
    SDL_Surface* surf = TTF_RenderUTF8_Blended(f, str.c_str(), sc);
    if (!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(sdl_, surf);
    SDL_Rect dr{x, y, surf->w, surf->h};
    SDL_RenderCopy(sdl_, tex, nullptr, &dr);
    SDL_DestroyTexture(tex);
    SDL_FreeSurface(surf);
}

SDL_Point Renderer::measureText(const std::string& str, int size, bool bold) {
    TTF_Font* f = getFont(size, bold);
    if (!f) return {0, 0};
    int w = 0, h = 0;
    TTF_SizeUTF8(f, str.c_str(), &w, &h);
    return {w, h};
}

void Renderer::textCentered(const std::string& str, int rx, int ry, int rw, int rh,
                             Color c, int size, bool bold) {
    auto dim = measureText(str, size, bold);
    int x = rx + (rw - dim.x) / 2;
    int y = ry + (rh - dim.y) / 2;
    text(str, x, y, c, size, bold);
}

std::string Renderer::truncate(const std::string& str, int maxWidth, int size, bool bold) {
    TTF_Font* f = getFont(size, bold);
    if (!f) return str;
    int w = 0, h = 0;
    TTF_SizeUTF8(f, str.c_str(), &w, &h);
    if (w <= maxWidth) return str;
    std::string s = str;
    while (!s.empty()) {
        s.pop_back();
        std::string t = s + "…";
        TTF_SizeUTF8(f, t.c_str(), &w, &h);
        if (w <= maxWidth) return t;
    }
    return "…";
}

SDL_Texture* Renderer::loadTexture(const std::string& path) {
    auto it = texCache_.find(path);
    if (it != texCache_.end()) return it->second;
    SDL_Texture* t = IMG_LoadTexture(sdl_, path.c_str());
    if (t) texCache_[path] = t;
    return t;
}

void Renderer::freeTexture(const std::string& path) {
    auto it = texCache_.find(path);
    if (it == texCache_.end()) return;
    SDL_DestroyTexture(it->second);
    texCache_.erase(it);
}

void Renderer::drawTexture(SDL_Texture* tex, int x, int y, int w, int h, float alpha) {
    if (!tex) return;
    SDL_SetTextureAlphaMod(tex, (uint8_t)(255.f * alpha));
    SDL_Rect r{x, y, w, h};
    SDL_RenderCopy(sdl_, tex, nullptr, &r);
}

void Renderer::drawTextureRounded(SDL_Texture* tex, int x, int y, int w, int h,
                                   int /*radius*/, float alpha) {
    // Simple: just draw the texture, rounded corners are hard without stencils
    // For PS4 SDL2 this is the pragmatic path
    drawTexture(tex, x, y, w, h, alpha);
}

void Renderer::progressBar(int x, int y, int w, int h, float progress,
                            Color bg, Color fg, int radius) {
    fillRoundRect(x, y, w, h, radius, bg);
    int fw = (int)(w * std::max(0.f, std::min(1.f, progress)));
    if (fw > 0) fillRoundRect(x, y, fw, h, radius, fg);
}

void Renderer::scrollBar(int x, int y, int h, float offset, float viewFraction) {
    Color track = fromHex(0x2A2A3AFF);
    Color thumb  = fromHex(COL_ACCENT);
    fillRoundRect(x, y, 6, h, 3, track);
    int th = (int)(h * viewFraction);
    int ty = y + (int)((h - th) * offset);
    fillRoundRect(x, ty, 6, th, 3, thumb);
}
