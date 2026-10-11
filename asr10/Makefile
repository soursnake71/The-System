# Builds the OS image and, given a stock OS disk, a bootable disk image.
#
#   make                      build/system.bin
#   make test                 unit tests for the disk tool
#   make disk STOCK=local/asr10-os353.img
#                             build/system.img: STOCK with its OS file
#                             replaced by build/system.bin
#   make disasm OS=local/os.bin ORG=0x...
#                             disassemble an extracted OS file

CROSS     ?= m68k-linux-gnu-
CC        := $(CROSS)gcc
OBJCOPY   := $(CROSS)objcopy
OBJDUMP   := $(CROSS)objdump

# Placeholders until the boot ROM's load address is known. MAME's skeleton lists
# OS RAM at 0xFB0000-0xFCFFFF; that is a guess there too.
LOAD_ADDR ?= 0x00FB0000
STACK_TOP ?= 0x00FD0000
OS_ENTRY  ?= 0

CFLAGS  := -m68000 -Os -ffreestanding -fno-builtin -nostdlib -Wall -Wextra -std=c99 \
           -DSTACK_TOP=$(STACK_TOP)
LDFLAGS := -m68000 -nostdlib -static -Wl,--build-id=none -Wl,--no-warn-rwx-segments -Wl,--defsym=LOAD_ADDR=$(LOAD_ADDR) -Wl,--defsym=STACK_TOP=$(STACK_TOP) \
           -T src/link.ld

SRCS := src/crt0.S src/main.c src/panel.c src/hw.c
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
	python3 tools/ensoniq_disk.py replace $(STOCK) $(OS_ENTRY) build/system.bin build/system.img

disasm:
	@test -n "$(OS)" || { echo "set OS=<extracted OS file>"; exit 1; }
	$(OBJDUMP) -D -b binary -m m68k:68000 --adjust-vma=$(or $(ORG),0) $(OS)

test:
	python3 -m unittest discover tests

clean:
	rm -rf build

.PHONY: all disk disasm test clean
