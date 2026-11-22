#pragma once

#include <SDL2/SDL_surface.h>
#include <string>

struct PreviewError {
    const char* stage = nullptr;
    int ioError = 0;
    std::string detail;
};

SDL_Surface* decodePreview(const std::string& bytes, PreviewError* error = nullptr);
SDL_Surface* loadPreviewFile(const std::string& path, PreviewError* error = nullptr);

inline constexpr int previewMaxEdge = 768;
// The caller keeps ownership of the source surface.
SDL_Surface* downsamplePreview(const SDL_Surface& image, PreviewError* error = nullptr);
