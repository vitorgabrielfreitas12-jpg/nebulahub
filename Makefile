TITLE := NEBULA HUB
TITLE_ID := NEBU00001
VERSION := 01.00
CONTENT_ID := IV0000-NEBU00001_00-NEBULAHUB0000001
OO_PS4_TOOLCHAIN ?= $(CURDIR)/.deps/OpenOrbis/PS4Toolchain
SDL_TTF_SRC ?= .deps/SDL_ttf.c
CC := clang-18
CXX := clang++-18
LD := ld.lld-18
SDK := $(abspath $(OO_PS4_TOOLCHAIN))
export OO_PS4_TOOLCHAIN := $(SDK)
TOOLS := $(SDK)/bin/linux
PKGTOOL_DLL := $(abspath .deps/pkgtool/PkgTool.Core.dll)
PKGTOOL := dotnet "$(PKGTOOL_DLL)"
FONTS ?= /usr/share/fonts/truetype/dejavu
BUILD := build
STAGE := $(BUILD)/pkg
CROSS := --target=x86_64-pc-freebsd12-elf --sysroot="$(SDK)" -nostdlibinc -fPIC -funwind-tables -D__ORBIS__ -D_GNU_SOURCE
CXXFLAGS := $(CROSS) -isystem "$(SDK)/include/c++/v1" -isystem "$(SDK)/include" -I.deps -isystem "$(SDK)/include/SDL2" -std=c++17 -O2 -Wall -Wextra
LDFLAGS := -m elf_x86_64 -pie --script "$(SDK)/link.x" --eh-frame-hdr -z max-page-size=0x4000 -L"$(SDK)/lib"
LIBS := -lSDL2_image -lSDL2 -lSceFreeType -lSceUserService -lSceAppInstUtil -lSceVideoOut -lSceAudioOut -lScePad -lSceSysmodule -lSceNet -lSceHttp -lSceSsl -lc++ -lc++abi -lunwind -lc -lkernel
SOURCES := $(wildcard src/*.cpp src/*/*.cpp)
OBJECTS := $(patsubst src/%.cpp,$(BUILD)/%.o,$(SOURCES)) $(BUILD)/SDL_ttf.o
METADATA := sce_sys/param.sfo sce_sys/icon0.png
PAYLOAD := eboot.bin sce_sys/about/right.sprx sce_module/libc.prx sce_module/libSceFios2.prx assets/fonts/DejaVuSans.ttf assets/fonts/DejaVuSans-Bold.ttf assets/fonts/LICENSE assets/misc/manifest.json
FILES := $(METADATA) $(PAYLOAD)

.DELETE_ON_ERROR:
.PHONY: all clean verify
all: nebulahub.pkg

$(BUILD)/%.o: src/%.cpp Makefile
	mkdir -p "$(@D)"
	$(CXX) $(CXXFLAGS) -MMD -MP -c "$<" -o "$@"

$(BUILD)/db/database.o: .deps/json.hpp

$(BUILD)/SDL_ttf.o: $(SDL_TTF_SRC) .deps/SDL_ttf.h Makefile
	mkdir -p "$(@D)"
	$(CC) $(CROSS) -isystem "$(SDK)/include" -isystem "$(SDK)/include/SDL2" -include proto-include.h -O2 -c "$<" -o "$@"

$(BUILD)/eboot.elf: $(OBJECTS) Makefile
	$(LD) $(LDFLAGS) -o "$@" "$(SDK)/lib/crt1.o" $(OBJECTS) --start-group $(LIBS) --end-group

$(STAGE)/eboot.bin: $(BUILD)/eboot.elf
	mkdir -p "$(@D)"
	"$(TOOLS)/create-fself" -in "$<" -out "$(BUILD)/eboot.oelf" --eboot "$@" --paid 0x3800000000000011
	test -s "$@"

$(STAGE)/sce_sys/param.sfo: Makefile $(PKGTOOL_DLL)
	mkdir -p "$(@D)"
	rm -f "$@.tmp"
	$(PKGTOOL) sfo_new "$@.tmp"
	$(PKGTOOL) sfo_setentry "$@.tmp" APP_TYPE --type Integer --maxsize 4 --value 1
	$(PKGTOOL) sfo_setentry "$@.tmp" APP_VER --type Utf8 --maxsize 8 --value "$(VERSION)"
	$(PKGTOOL) sfo_setentry "$@.tmp" ATTRIBUTE --type Integer --maxsize 4 --value 0
	$(PKGTOOL) sfo_setentry "$@.tmp" CATEGORY --type Utf8 --maxsize 4 --value gd
	$(PKGTOOL) sfo_setentry "$@.tmp" CONTENT_ID --type Utf8 --maxsize 48 --value "$(CONTENT_ID)"
	$(PKGTOOL) sfo_setentry "$@.tmp" DOWNLOAD_DATA_SIZE --type Integer --maxsize 4 --value 0
	$(PKGTOOL) sfo_setentry "$@.tmp" SYSTEM_VER --type Integer --maxsize 4 --value 0
	$(PKGTOOL) sfo_setentry "$@.tmp" TITLE --type Utf8 --maxsize 128 --value "$(TITLE)"
	$(PKGTOOL) sfo_setentry "$@.tmp" TITLE_ID --type Utf8 --maxsize 12 --value "$(TITLE_ID)"
	$(PKGTOOL) sfo_setentry "$@.tmp" VERSION --type Utf8 --maxsize 8 --value "$(VERSION)"
	mv "$@.tmp" "$@"

$(STAGE)/sce_sys/icon0.png: Makefile
	mkdir -p "$(@D)"
	convert -size 512x512 gradient:'#0D0D1A-#1A0A30' -gravity Center \
	  -font "$(FONTS)/DejaVuSans-Bold.ttf" -pointsize 52 -fill '#9B4FFF' \
	  -annotate 0 "$(TITLE)" -strip PNG32:"$@"

$(STAGE)/sce_sys/about/right.sprx: $(SDK)/samples/SDL2/sce_sys/about/right.sprx
	mkdir -p "$(@D)"
	cp "$<" "$@"

$(STAGE)/sce_module/%.prx: $(SDK)/samples/SDL2/sce_module/%.prx
	mkdir -p "$(@D)"
	cp "$<" "$@"

$(STAGE)/assets/fonts/DejaVuSans.ttf: $(FONTS)/DejaVuSans.ttf
	mkdir -p "$(@D)"
	cp "$<" "$@"

$(STAGE)/assets/fonts/DejaVuSans-Bold.ttf: $(FONTS)/DejaVuSans-Bold.ttf
	mkdir -p "$(@D)"
	cp "$<" "$@"

$(STAGE)/assets/fonts/LICENSE: /usr/share/doc/fonts-dejavu-core/copyright
	mkdir -p "$(@D)"
	cp "$<" "$@"

$(STAGE)/assets/misc/manifest.json: manifest/manifest.json
	mkdir -p "$(@D)"
	cp "$<" "$@"

$(STAGE)/project.gp4: $(addprefix $(STAGE)/,$(FILES)) Makefile
	cd "$(STAGE)" && "$(TOOLS)/create-gp4" -out project.gp4 --content-id="$(CONTENT_ID)" --files "$(FILES)"

nebulahub.pkg: $(STAGE)/project.gp4 $(PKGTOOL_DLL)
	cd "$(STAGE)" && $(PKGTOOL) pkg_build project.gp4 .
	test -s "$(STAGE)/$(CONTENT_ID).pkg"
	mv "$(STAGE)/$(CONTENT_ID).pkg" "$@"

verify: nebulahub.pkg
	readelf -h "$(BUILD)/eboot.elf"
	readelf -h "$(BUILD)/eboot.oelf"
	$(PKGTOOL) pkg_validate --verbose "$<" > "$(BUILD)/pkg-validation.log"
	cat "$(BUILD)/pkg-validation.log"
	! grep -F '[ERROR]' "$(BUILD)/pkg-validation.log"
	test "$$(od -An -tx1 -N4 "$(STAGE)/eboot.bin" | tr -d '[:space:]')" = 4f153d1d
	$(PKGTOOL) pkg_listentries "$<" > "$(BUILD)/pkg-entries.log"
	cat "$(BUILD)/pkg-entries.log"
	$(PKGTOOL) pkg_extract --passcode 00000000000000000000000000000000 "$<" "$(BUILD)/extracted"
	@for file in $(PAYLOAD); do cmp "$(STAGE)/$$file" "$(BUILD)/extracted/uroot/$$file" || exit 1; done
	$(PKGTOOL) pkg_extractentry "$<" "$$(awk '$$NF == "PARAM_SFO" {print $$4}' "$(BUILD)/pkg-entries.log")" "$(BUILD)/extracted/param.sfo"
	$(PKGTOOL) pkg_extractentry "$<" "$$(awk '$$NF == "ICON0_PNG" {print $$4}' "$(BUILD)/pkg-entries.log")" "$(BUILD)/extracted/icon0.png"
	cmp "$(STAGE)/sce_sys/icon0.png" "$(BUILD)/extracted/icon0.png"
	$(PKGTOOL) sfo_listentries "$(STAGE)/sce_sys/param.sfo" > "$(BUILD)/staged-sfo.txt"
	$(PKGTOOL) sfo_listentries "$(BUILD)/extracted/param.sfo" > "$(BUILD)/packed-sfo.txt"
	sed '/^PUBTOOLINFO : /d; /^PUBTOOLVER : /d' "$(BUILD)/packed-sfo.txt" | diff - "$(BUILD)/staged-sfo.txt"

clean:
	rm -rf "$(BUILD)" nebulahub.pkg

-include $(OBJECTS:.o=.d)
