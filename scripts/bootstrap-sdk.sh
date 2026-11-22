#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p .deps
tmp="$(mktemp -d "$PWD/.deps/download.XXXXXXXX")"
trap 'rm -rf "$tmp"' EXIT

curl --fail --silent --show-error --location --retry 3 \
    https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/releases/download/v0.5.4/toolchain-llvm-18.tar.gz \
    -o "$tmp/openorbis.tar.gz"
curl --fail --silent --show-error --location --retry 3 \
    https://raw.githubusercontent.com/libsdl-org/SDL_ttf/release-2.0.15/SDL_ttf.c \
    -o "$tmp/SDL_ttf.c"
curl --fail --silent --show-error --location --retry 3 \
    https://raw.githubusercontent.com/libsdl-org/SDL_ttf/release-2.0.15/SDL_ttf.h \
    -o "$tmp/SDL_ttf.h"
curl --fail --silent --show-error --location --retry 3 \
    https://raw.githubusercontent.com/nlohmann/json/v3.11.3/single_include/nlohmann/json.hpp \
    -o "$tmp/json.hpp"
curl --fail --silent --show-error --location --retry 3 \
    https://codeload.github.com/OpenOrbis/LibOrbisPkg/tar.gz/92a4e7f8e5914cb06b52ba3ae6b0675da438a051 \
    -o "$tmp/liborbis-pkg.tar.gz"
(cd "$tmp" && sha256sum --check <<'SUMS'
3c7cd5bb593ca74fa1c13fd59f3938dc0fc07985167f7275063019e63abe4526  openorbis.tar.gz
4d76797b639a86c40b28947803e1d267256378384617920f77152e500d2c9311  SDL_ttf.c
d8812df3f415637d49ea647d66d10d4ba7cead505445a5d8e6d089a92052d58b  SDL_ttf.h
9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6  json.hpp
fa22db4164dfbeeb76f22c68d519c76bcf5fc608815070b576464f7272c73c3e  liborbis-pkg.tar.gz
SUMS
)
tar -xzf "$tmp/openorbis.tar.gz" -C .deps
mv "$tmp/SDL_ttf.c" "$tmp/SDL_ttf.h" "$tmp/json.hpp" .deps/
mkdir -p .deps/liborbis-pkg
tar -xzf "$tmp/liborbis-pkg.tar.gz" -C .deps/liborbis-pkg --strip-components=1
dotnet build .deps/liborbis-pkg/PkgTool.Core/PkgTool.Core.csproj \
    --configuration Release --output .deps/pkgtool -p:NuGetAudit=false
