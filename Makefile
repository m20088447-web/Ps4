TITLE       := PhoneCast
VERSION     := 1.00
TITLE_ID    := PHON00001
CONTENT_ID  := IV0000-PHON00001_00-PHONECAST0000001

TOOLCHAIN   ?= $(OO_PS4_TOOLCHAIN)
ifeq ($(strip $(TOOLCHAIN)),)
$(error Set OO_PS4_TOOLCHAIN to an extracted OpenOrbis PS4 Toolchain v0.5.4 directory)
endif

# Locate the SDL2 headers/libs shipped inside the OpenOrbis toolchain. The
# directory layout differs between releases, so search instead of hard-coding.
SDL_HEADER  := $(firstword $(shell find "$(TOOLCHAIN)/include" "$(TOOLCHAIN)/samples" -name SDL.h 2>/dev/null))
SDL_INC     := $(patsubst %/SDL.h,%,$(SDL_HEADER))
ifeq ($(strip $(SDL_INC)),)
$(error SDL.h not found under $(TOOLCHAIN)/include or $(TOOLCHAIN)/samples)
endif
SDL_LIBFILE := $(firstword $(shell find "$(TOOLCHAIN)/lib" "$(TOOLCHAIN)/samples" \( -name 'libSDL2.a' -o -name 'libSDL2.so' -o -name 'libSDL2.prx' \) 2>/dev/null))
SDL_LIBDIR  := $(patsubst %/,%,$(dir $(SDL_LIBFILE)))
$(info SDL include dir: $(SDL_INC))
$(info SDL library file: $(SDL_LIBFILE))

PROJDIR     := $(CURDIR)
INTDIR      := build
CDIR        := linux
CC          := clang-18
CCX         := clang++-18
LD          := ld.lld-18
PKGTOOL     := $(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core
CREATE_GP4  := $(TOOLCHAIN)/bin/$(CDIR)/create-gp4
CREATE_FSELF:= $(TOOLCHAIN)/bin/$(CDIR)/create-fself

LIBS        := -lc -lkernel -lc++ -lSceUserService -lSceVideoOut \
               -lScePad -lSceSysmodule -lSceNet -lSceNetCtl \
               -lSceAvPlayer -lSDL2
COMMON_CXXFLAGS := --target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables \
                   -O2 -Wall -Wextra -Wno-unused-parameter \
                   -isysroot $(TOOLCHAIN) -isystem $(TOOLCHAIN)/include \
                   -isystem $(TOOLCHAIN)/include/c++/v1 -isystem $(SDL_INC) -Iinclude -std=c++17
LDFLAGS     := -m elf_x86_64 -pie --script $(TOOLCHAIN)/link.x --eh-frame-hdr \
               -L$(TOOLCHAIN)/lib $(if $(SDL_LIBDIR),-L$(SDL_LIBDIR)) $(LIBS) $(TOOLCHAIN)/lib/crt1.o
OBJECTS     := $(INTDIR)/main.o
MODULES_SRC := $(TOOLCHAIN)/samples/SDL2/sce_module
MODULES     = $(wildcard sce_module/*)
PKG_FILES   = eboot.bin sce_sys/param.sfo sce_sys/icon0.png $(MODULES)

.PHONY: all clean package prepare modules
all: $(CONTENT_ID).pkg

prepare: sce_sys/icon0.png sce_sys/param.sfo modules

modules:
	mkdir -p sce_module
	@if [ -d "$(MODULES_SRC)" ]; then cp -n "$(MODULES_SRC)"/* sce_module/ 2>/dev/null || true; fi

sce_sys/icon0.png:
	mkdir -p sce_sys
	python3 tools/gen_icon.py

sce_sys/param.sfo: Makefile
	mkdir -p sce_sys
	$(PKGTOOL) sfo_new $@
	$(PKGTOOL) sfo_setentry $@ APP_TYPE --type Integer --maxsize 4 --value 1
	$(PKGTOOL) sfo_setentry $@ APP_VER --type Utf8 --maxsize 8 --value '$(VERSION)'
	$(PKGTOOL) sfo_setentry $@ ATTRIBUTE --type Integer --maxsize 4 --value 0
	$(PKGTOOL) sfo_setentry $@ CATEGORY --type Utf8 --maxsize 4 --value 'gd'
	$(PKGTOOL) sfo_setentry $@ CONTENT_ID --type Utf8 --maxsize 48 --value '$(CONTENT_ID)'
	$(PKGTOOL) sfo_setentry $@ DOWNLOAD_DATA_SIZE --type Integer --maxsize 4 --value 0
	$(PKGTOOL) sfo_setentry $@ SYSTEM_VER --type Integer --maxsize 4 --value 0
	$(PKGTOOL) sfo_setentry $@ TITLE --type Utf8 --maxsize 128 --value '$(TITLE)'
	$(PKGTOOL) sfo_setentry $@ TITLE_ID --type Utf8 --maxsize 12 --value '$(TITLE_ID)'
	$(PKGTOOL) sfo_setentry $@ VERSION --type Utf8 --maxsize 8 --value '$(VERSION)'

$(INTDIR):
	mkdir -p $@

$(INTDIR)/main.o: src/main.cpp include/avplayer_abi.h include/ds4_controls.h | $(INTDIR)
	$(CCX) $(COMMON_CXXFLAGS) -c $< -o $@

$(INTDIR)/PhoneCast.elf: $(OBJECTS)
	$(LD) $(OBJECTS) -o $@ $(LDFLAGS)

eboot.bin: $(INTDIR)/PhoneCast.elf
	$(CREATE_FSELF) -in=$< -out=$(INTDIR)/PhoneCast.oelf --eboot "eboot.bin" --paid 0x3800000000000011

pkg.gp4: eboot.bin sce_sys/param.sfo sce_sys/icon0.png modules
	$(CREATE_GP4) -out $@ --content-id=$(CONTENT_ID) --files "$(PKG_FILES)"

$(CONTENT_ID).pkg: pkg.gp4
	$(PKGTOOL) pkg_build $< .

package: $(CONTENT_ID).pkg

clean:
	rm -rf $(INTDIR) PhoneCast.pkg pkg.gp4 eboot.bin sce_sys/param.sfo sce_sys/icon0.png

