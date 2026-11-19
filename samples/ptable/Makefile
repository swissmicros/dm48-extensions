# SPDX-License-Identifier: MIT
# Copyright © 2026 SwissMicros GmbH
#
# Builds the ptable sample: `make` gives ptable.ext (a copy of template/Makefile: NAME, SRCS, KIT = ../.., the data and the tests).
#   make            NAME.ext for the calculator (ARM), with the web tester's WebAssembly build inside when the
#                   wasm compiler is there (WASM_CC, below)
#   make wasm       NAME.wasm alone (fails without WASM_CC); `make wasm all` insists on both sections
#   make host       NAME.so, a native build for the host simulator (fwcli --ext DIR, Linux)
#   make clean

NAME    = ptable
VERSION = 1
SRCS    = ptable.c chem.c table.c

KIT     = ../..
CC      = arm-none-eabi-gcc
PYTHON  = python3

# The DM42n's Cortex-M33 runs the DMCP armv7e-m hard-float code: the dx_* functions pass doubles in FPU registers.
CPUFLAGS = -mthumb -march=armv7e-m -mfloat-abi=hard -mfpu=fpv4-sp-d16
# -mword-relocations: addresses as literal-pool words (R_ARM_ABS32), never MOVW/MOVT pairs the loader cannot patch.
# -fno-builtin -fno-tree-loop-distribute-patterns: gcc must not turn loops into memcpy/memset calls (there is no libc).
# -Werror=implicit-function-declaration: a misspelt dx_ name fails the build instead of becoming an import.
CFLAGS  = $(CPUFLAGS) -Os -ffunction-sections -fdata-sections -fno-common -nostdlib -ffreestanding -fno-builtin \
          -fno-tree-loop-distribute-patterns -mword-relocations -Wall -Wextra \
          -Werror=implicit-function-declaration -I$(KIT)/include
# -q keeps the relocations for extpack.py; the dx_* imports stay undefined until the calculator loads the file.
# libgcc gives the double arithmetic and 64-bit division helpers (the FPU is single precision).
LDFLAGS = -T $(KIT)/ld/ext.ld -Wl,-q -Wl,--unresolved-symbols=ignore-all -Wl,--gc-sections
LDLIBS  = -lgcc

# The web tester's build, packed into the .ext as its WebAssembly section: clang for wasm32 with its linker wasm-ld,
# as the Emscripten SDK ships them (https://emscripten.org; EMSDK, or ~/emsdk). No libc here either: -nostdlib, and
# no --allow-undefined, so a call of anything but a dx_* function fails the link. The dx_* functions are imported
# from module "dx" by name@major (dm48ext.h includes dm48ext_wasm.h for __wasm__). The module exports dx_init,
# its memory and its function table (--export-table): a command's dx_cmd_fn is an index in that table, which the
# tester calls.
EMSDK   ?= $(HOME)/emsdk
WASM_CC ?= $(EMSDK)/upstream/bin/clang
WASM_CFLAGS  = --target=wasm32 -O2 -nostdlib -ffreestanding -Wall -Wextra -Werror=implicit-function-declaration \
               -I$(KIT)/include
WASM_LDFLAGS = -Wl,--no-entry -Wl,--export=dx_init -Wl,--export-dynamic -Wl,--export-table
HAVE_WASM_CC = $(shell command -v $(WASM_CC) 2>/dev/null)
WASM     = $(if $(HAVE_WASM_CC),$(NAME).wasm)

OBJS = $(SRCS:.c=.o)

all: $(NAME).ext

$(NAME).ext: $(NAME).elf $(WASM) $(KIT)/tools/extpack.py
	$(PYTHON) $(KIT)/tools/extpack.py $< -o $@ --name $(NAME) --version $(VERSION) $(if $(WASM),--wasm $(WASM))

$(NAME).elf: $(OBJS) $(KIT)/ld/ext.ld
	$(CC) $(CFLAGS) $(OBJS) $(LDFLAGS) $(LDLIBS) -o $@

%.o: %.c $(KIT)/include/dm48ext.h
	$(CC) $(CFLAGS) -c $< -o $@

# The element data: elements.csv is the source, mkdata.py turns it into C initialisers (elements.inc) for chem.c.
elements.inc: elements.csv mkdata.py
	$(PYTHON) mkdata.py elements.csv > $@.tmp && mv $@.tmp $@

chem.o $(NAME).so $(NAME).wasm: elements.inc

# The host tests (cc, with the sanitizers): the data, the lookups, MOLMASS, ELEM, the layout and PTABLE's key loop, with
# stub dx_* functions. They also write ptable.pbm, the screen as PTABLE first draws it.
TEST_SRCS = chem.c

test: test_ptable
	./test_ptable

test_ptable: test_ptable.c $(TEST_SRCS) chem.h elements.inc $(KIT)/include/dm48ext.h
	cc -O1 -g -Wall -Wextra -fsanitize=address,undefined -I$(KIT)/include -o $@ test_ptable.c $(TEST_SRCS)

# The host simulator build: a shared object for fwcli, which provides the dx_* functions itself (no core sources here).
host: $(NAME).so

$(NAME).so: $(SRCS) $(KIT)/include/dm48ext.h
	cc -shared -fPIC -O2 -Wall -Werror=implicit-function-declaration -I$(KIT)/include -o $@ $(SRCS)

ifeq ($(HAVE_WASM_CC),)
wasm:
	@echo "make wasm: no $(WASM_CC) (install the Emscripten SDK, or set EMSDK or WASM_CC)" >&2; exit 1
else
wasm: $(NAME).wasm
endif

$(NAME).wasm: $(SRCS) $(KIT)/include/dm48ext.h $(KIT)/include/dm48ext_wasm.h
	$(WASM_CC) $(WASM_CFLAGS) $(SRCS) $(WASM_LDFLAGS) -o $@

clean:
	rm -f $(OBJS) $(NAME).elf $(NAME).ext $(NAME).wasm $(NAME).so elements.inc test_ptable ptable.pbm

.PHONY: all host wasm clean test
