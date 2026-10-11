# Builds the OS image and, given a stock OS disk, a bootable disk image.
#
#   make                      build/system.bin
#   make test                 unit tests for the disk tool
#   make disk STOCK=local/asr10-os353.img
#                             build/system.img: a copy of STOCK whose OS file
#                             is replaced by ours (header and load table
#                             taken from STOCK's OS, see tools/make_os.py)
#   make hfe STOCK=...        build/THESYSTEM.hfe, the same disk for a Gotek
#                             that uses HFE files (STOCK may be .img or .hfe)
#   make disasm OS=local/os.bin ORG=0x...
#                             disassemble an extracted OS file

CROSS     ?= m68k-linux-gnu-
CC        := $(CROSS)gcc
OBJCOPY   := $(CROSS)objcopy
OBJDUMP   := $(CROSS)objdump

# Our code loads at the start of the stock OS's first segment (0x0E00 in
# OS 1.61). The stack grows down from 0x0A00 into 0x400-0x9FF, RAM the
# stock OS file doesn't load anything into.
LOAD_ADDR ?= 0x0E00
STACK_TOP ?= 0x0A00

CFLAGS  := -m68000 -Os -ffreestanding -fno-builtin -nostdlib -Wall -Wextra -std=c99 \
           -DSTACK_TOP=$(STACK_TOP)
LDFLAGS := -m68000 -nostdlib -static -Wl,--build-id=none -Wl,--no-warn-rwx-segments -Wl,--defsym=LOAD_ADDR=$(LOAD_ADDR) -Wl,--defsym=STACK_TOP=$(STACK_TOP) \
           -T src/link.ld

SRCS := src/crt0.S src/main.c src/hw.c
OBJS := $(patsubst src/%,build/%.o,$(SRCS))

all: build/system.bin

build/%.o: src/%
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/system.elf: $(OBJS) src/link.ld
	$(CC) $(LDFLAGS) $(OBJS) -lgcc -o $@

build/system.bin: build/system.elf
	$(OBJCOPY) -O binary $< $@
	@echo "$@: $$(stat -c %s $@) bytes, linked at $(LOAD_ADDR)"

disk: build/system.bin
	@test -n "$(STOCK)" || { echo "set STOCK=<stock OS disk image>"; exit 1; }
	@case "$(STOCK)" in *.hfe|*.HFE) python3 tools/hfe.py to-img $(STOCK) build/stock.img ;; \
	  *) cp $(STOCK) build/stock.img ;; esac
	python3 tools/ensoniq_disk.py extract build/stock.img os build/stock-os.bin
	python3 tools/make_os.py build/stock-os.bin build/system.bin $(LOAD_ADDR) build/system-os.bin
	python3 tools/ensoniq_disk.py replace build/stock.img os build/system-os.bin build/system.img

hfe: disk
	python3 tools/hfe.py to-hfe build/system.img build/THESYSTEM.hfe

disasm:
	@test -n "$(OS)" || { echo "set OS=<extracted OS file>"; exit 1; }
	$(OBJDUMP) -D -b binary -m m68k:68000 --adjust-vma=$(or $(ORG),0) $(OS)

test: build/system.bin
	python3 -m unittest discover tests

clean:
	rm -rf build

.PHONY: all disk hfe disasm test clean
