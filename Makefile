# SGDK project Makefile.
#
# SGDK's makefile.gen does all the work: it discovers src/**/*.c, res/**/*.res,
# runs rescomp, compiles for m68k, links, and emits out/rom.bin (the ELF is
# out/release/rom.out and a symbol map out/release/symbol.txt).
#
# Usage:
#   export GDK=/opt/sgdk          # or wherever SGDK lives (docker image sets this)
#   make            -> out/rom.bin        (release: -O3 -flto)
#   make debug      -> out/rom.bin        (-O1 -ggdb, asserts, KDebug/KLog)
#   make asm        -> release + .lst assembly listings
#   make clean      -> wipe out/   (do this after changing a shared struct layout
#                                   or any SGDK header: makefile.gen tracks your
#                                   own headers but not $GDK/inc, nor headers
#                                   newly nested inside other headers, and stale
#                                   objects built against an old layout cause
#                                   "impossible" crashes)
#
# Inject a define for one build (the flag below is appended, not replaced):
#   make EXTRA_FLAGS=-DSELFTEST

GDK ?= /home/claude/work/sgdk
export GDK

# The cartridge (5 MB) is the SGDK program followed by one compact pack per
# episode and the music banks (inc/jj1_pack.h, tools/build_cartridge.py).  The
# packs are built first, because the program carries their addresses
# (out/lite/lite_packdir.inc) and the music's (out/lite/jj1_music.h); the ROM
# is assembled once SGDK has produced the program.  `make` leaves it in
# out/rom.bin and JazzJackrabbitGenesis.bin.
#
# The music is converted once from the game's modules and kept in
# packs/music: the first build needs  make JAZZ_DATA=/path/to/JAZZ
# (the shareware directory is enough for episode 1).

# Must be set BEFORE the include: makefile.gen folds EXTRA_FLAGS into
# DEFAULT_FLAGS with a simply-expanded (:=) assignment, so a later value is
# silently ignored.
#
# -fno-store-merging: GCC (verified on 13.3, -O3 -m68000) merges adjacent byte
# stores into one word store, e.g. two u8 struct fields at offsets 11 and 12
# become `move.w ...,11(a0)` — a word access at an ODD address, which is an
# address error on the 68000. Cheap insurance for every project.
# `override ... +=` appends even when EXTRA_FLAGS comes from the command line.
override EXTRA_FLAGS += -fno-store-merging -DJAZZ_JJ1_RUNTIME -mstrict-align -fno-tree-slp-vectorize \
    -I$(CURDIR)/out/lite

include $(GDK)/makefile.gen

# Safe to set AFTER the include (used inside the link recipe, expanded late).
# - SGDK's own lib/libgcc.a is built for the 68000. On Linux, common.mk
#   defaults to -lgcc, which with Debian/Ubuntu's gcc-m68k-linux-gnu is a
#   68020+ build: its __divsi3 uses instructions that trap on a 68000.
# - --build-id=none: Linux-target binutils emit .note.gnu.build-id at address 0,
#   colliding with the 68000 vector table. Harmless with a real m68k-elf toolchain.
LIBGCC := $(GDK)/lib/libgcc.a -Wl,--build-id=none

# SGDK 2.11 used boot/rom_head.c plus an embedded header, whereas newer
# releases compile src/rom_header.c automatically. Do not link it twice on
# the older release; compile the project's header into the boot binary.
ifeq ($(wildcard $(GDK)/src/boot/rom_header.c),)
OBJS := $(filter-out $(OUT)/src/rom_header.o,$(OBJS))
$(OUT)/rom_head.o: src/rom_header.c
	$(MKDIR) -p $(dir $@)
	$(CC) $(DEFAULT_FLAGS) -c src/rom_header.c -o $@
endif

# rescomp creates headers as a side effect of compiling each resource. Ensure
# they exist before compiling C files, including on parallel clean builds.
LITE_DIR := out/lite
.PHONY: cartridge
out/packs/stamp:
	python3 tools/build_packs.py packs
$(LITE_DIR)/lite_packdir.inc: out/packs/stamp
	python3 tools/build_cartridge.py packs $(if $(JAZZ_DATA),--input $(JAZZ_DATA))
$(OBJS): | $(LITE_DIR)/lite_packdir.inc
release: cartridge
debug: cartridge
cartridge: padROM
	python3 tools/build_cartridge.py rom --program $(OUT_DIR)/rom.bin
$(OUT)/src/main.o $(OUT)/src/bonus_view.o $(OUT)/src/jj1_scenes.o: \
  $(OUT)/res/jj1_sounds.o $(OUT)/res/jj1_font.o \
  $(OUT)/res/jj1_bonus.o $(OUT)/res/jj1_hud.o $(OUT)/res/jj1_scenes_res.o
