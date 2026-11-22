// NEBULA HUB — Renderer (SDL2 wrapper)

#include "renderer.h"
#include <cstring>
#include <cmath>
#include <algorithm>

Renderer::Renderer(SDL_Renderer* r) : sdl_(r), previewTextures_(r) {}

Renderer::~Renderer() {
    for (auto& [k, t] : textCache_) SDL_DestroyTexture(t);
    for (auto& [k, f] : fonts_) TTF_CloseFont(f);
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
    SDL_SetRenderDrawBlendMode(sdl_, c.a == 255 && alpha == 1.f ? SDL_BLENDMODE_NONE : SDL_BLENDMODE_BLEND);
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
    if (w <= 0 || h <= 0) return;
    radius = std::max(0, std::min(radius, std::min(w, h) / 2));
    if (!radius) {
        fillRect(x, y, w, h, c, alpha);
        return;
    }
    SDL_Rect bands[65];
    radius = std::min(radius, 32);
    bands[0] = {x, y + radius, w, h - 2 * radius};
    for (int row = 0; row < radius; ++row) {
        const int dy = radius - row - 1;
        const int inset = radius - static_cast<int>(std::sqrt(radius * radius - dy * dy));
        bands[1 + row * 2] = {x + inset, y + row, w - 2 * inset, 1};
        bands[2 + row * 2] = {x + inset, y + h - row - 1, w - 2 * inset, 1};
    }
    const auto opacity = static_cast<uint8_t>(c.a * alpha);
    SDL_SetRenderDrawBlendMode(sdl_, opacity == 255 ? SDL_BLENDMODE_NONE : SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sdl_, c.r, c.g, c.b, opacity);
    SDL_RenderFillRects(sdl_, bands, 1 + 2 * radius);
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
    if (shadow) text(str, x + 2, y + 2, {0, 0, 0, 160}, size, bold);
    const std::string key = fontKey(size, bold) + ":" +
        std::to_string(c.r | (uint32_t(c.g) << 8) | (uint32_t(c.b) << 16) | (uint32_t(c.a) << 24)) + ":" + str;
    auto it = textCache_.find(key);
    if (it == textCache_.end()) {
        TTF_Font* font = getFont(size, bold);
        if (!font) return;
        SDL_Surface* surface = TTF_RenderUTF8_Blended(font, str.c_str(), {c.r, c.g, c.b, c.a});
        if (!surface) return;
        SDL_Texture* texture = SDL_CreateTextureFromSurface(sdl_, surface);
        SDL_FreeSurface(surface);
        if (!texture) return;
        if (textCache_.size() >= 384) {
            SDL_DestroyTexture(textCache_.at(textOrder_.front()));
            textCache_.erase(textOrder_.front());
            textOrder_.pop_front();
        }
        textOrder_.push_back(key);
        it = textCache_.emplace(key, texture).first;
    }
    SDL_Rect rect{x, y, 0, 0};
    SDL_QueryTexture(it->second, nullptr, nullptr, &rect.w, &rect.h);
    SDL_RenderCopy(sdl_, it->second, nullptr, &rect);
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
