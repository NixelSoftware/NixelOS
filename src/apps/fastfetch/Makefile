# Independent, opt-in build. Never invokes the OS image builder.
CC = gcc
NASM ?= nasm
LD = ld
OBJCOPY ?= objcopy
PYTHON ?= python3
CFLAGS = -O2 -m32 -ffreestanding -fno-pie -fno-stack-protector -fno-leading-underscore -Wall -Wextra

ifeq ($(OS),Windows_NT)
    LD_EMULATION = i386pe
    NASM_FMT     = win32
	PYTHON := python
else
    LD_EMULATION = elf_i386
    NASM_FMT     = elf32
endif

.PHONY: all package clean FORCE_VERSION
all: fastfetch.bin

entry.o: entry.asm
	$(NASM) -f $(NASM_FMT) $< -o $@

logo.h: ../../../resources/nixelos-trans.ansi.txt generate_logo.py
	$(PYTHON) generate_logo.py $< $@

../../../src/include/os_version.h: FORCE_VERSION ../../../VERSION ../../../tools/generate_version.py
	$(PYTHON) ../../../tools/generate_version.py

main.o: ../../../src/include/os_version.h main.c logo.h ../../system/user/sys_api.h ../../system/user/memory_api.h ../../kernel/types.h ../../kernel/fs.h ../../kernel/hal/net_types.h ../../system/user/window_api.h
	$(CC) $(CFLAGS) -c main.c -o $@

fastfetch.elf: entry.o main.o
	$(LD) -m $(LD_EMULATION) -Ttext 0xC00000 -e _start $^ -o $@

fastfetch.bin: fastfetch.elf
	$(OBJCOPY) -O binary --remove-section=.bss $< $@

package: fastfetch.bin
	$(PYTHON) -c "import tarfile; t=tarfile.open('fastfetch-0.1.0-i386.tar.gz','w:gz'); [t.add(p,arcname=p) for p in ('fastfetch.bin','README.md')]; t.close()"

clean:
	$(PYTHON) -c "from pathlib import Path; [Path(p).unlink(missing_ok=True) for p in ('entry.o','main.o','fastfetch.elf','fastfetch.bin','fastfetch-0.1.0-i386.tar.gz')]"
