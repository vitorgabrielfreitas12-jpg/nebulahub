#pragma once

#include <SDL2/SDL_surface.h>
#include <string>

SDL_Surface* decodePreview(const std::string& bytes);
