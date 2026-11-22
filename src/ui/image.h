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
