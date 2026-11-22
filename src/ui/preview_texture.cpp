#include "preview_texture.h"
#include "../diagnostics.h"

#include <algorithm>

bool PreviewTextures::draw(const std::string& id, const std::string& source,
                           SDL_Surface* image, SDL_Rect bounds) {
    constexpr size_t maxBytes = 16 * 1024 * 1024;
    const auto key = id + '\n' + source;
    auto found = textures_.find(key);
    if (found == textures_.end()) {
        if (!image) return false;
        while (order_.size() >= 24) {
            bytes_ -= textures_.at(order_.front()).bytes;
            textures_.erase(order_.front());
            order_.pop_front();
        }
        found = textures_.try_emplace(key).first;
        order_.push_back(key);
    }
    auto& texture = found->second;
    if (!texture.handle) {
        const auto now = std::chrono::steady_clock::now();
        if (!image || now < texture.retry) return false;
        const size_t bytes = size_t(image->w) * image->h * 4;
        if (bytes > maxBytes) return false;
        for (auto oldest = order_.begin(); bytes_ + bytes > maxBytes;) {
            if (*oldest == key) {
                ++oldest;
                continue;
            }
            bytes_ -= textures_.at(*oldest).bytes;
            textures_.erase(*oldest);
            oldest = order_.erase(oldest);
        }
        // The PS4 SDL2 version reads this hint at texture creation.
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
        texture.handle.reset(SDL_CreateTextureFromSurface(renderer_, image));
        uint32_t format = 0;
        if (!texture.handle ||
            SDL_QueryTexture(texture.handle.get(), &format, nullptr, &texture.width, &texture.height) < 0 ||
            texture.width <= 0 || texture.height <= 0) {
            diagnostic("preview", "Create or query texture failed", ErrorDomain::Application, 0,
                       (id + " " + SDL_GetError()).c_str());
            texture.handle.reset();
            texture.retry = now + std::chrono::seconds(5);
            return false;
        }
        texture.bytes = bytes;
        bytes_ += bytes;
        diagnostic("preview", "Texture created", ErrorDomain::Event, 0,
                   (id + " " + std::to_string(texture.width) + "x" + std::to_string(texture.height) +
                    " " + SDL_GetPixelFormatName(format)).c_str());
    }

    if (bounds.w <= 0 || bounds.h <= 0) return false;
    const auto scale = std::min(double(bounds.w) / texture.width, double(bounds.h) / texture.height);
    const auto width = std::max(1, int(texture.width * scale));
    const auto height = std::max(1, int(texture.height * scale));
    SDL_Rect destination{bounds.x + (bounds.w - width) / 2, bounds.y + (bounds.h - height) / 2,
                         width, height};
    const bool drawn = copy_(renderer_, texture.handle.get(), nullptr, &destination) == 0;
    if (!drawn && !texture.drawFailed) {
        diagnostic("preview", "Render copy failed", ErrorDomain::Application, 0, (id + " " + SDL_GetError()).c_str());
    } else if (drawn && (!texture.drawn || texture.drawFailed)) {
        diagnostic("preview", "Texture rendered", ErrorDomain::Event, 0,
                   (id + " x=" + std::to_string(destination.x) + " y=" + std::to_string(destination.y) +
                    " w=" + std::to_string(width) + " h=" + std::to_string(height)).c_str());
        texture.drawn = true;
    }
    texture.drawFailed = !drawn;
    return drawn;
}
