# GNU Make is the single build entry point on Windows and Linux.
PYTHON ?= python3
NASM ?= nasm
CC = gcc
LD ?= ld
OBJCOPY ?= objcopy
QEMU ?= qemu-system-i386

ENABLE_OOBE ?= 0

ifeq ($(OS),Windows_NT)
  NASM_FORMAT := win32
  LD_FORMAT := i386pe
  LDFLAGS := -m $(LD_FORMAT) --image-base 0 --disable-reloc-section
  PYTHON := python
else
  NASM_FORMAT := elf32
  LD_FORMAT := elf_i386
  LDFLAGS := -m $(LD_FORMAT)
endif

DEBUG_SUBSYSTEMS ?= 0
ifneq ($(DEBUG_SUBSYSTEMS),0)
ifneq ($(DEBUG_SUBSYSTEMS),1)
$(error DEBUG_SUBSYSTEMS must be 0 or 1)
endif
endif

ifeq ($(ENABLE_OOBE),1)
  OOBE_VAL += -DENABLE_OOBE=1
  OOBE_APP := oobe
else
  OOBE_APP :=
endif

CFLAGS := -O2 -m32 -ffreestanding -fno-leading-underscore -fno-pie \
          -mno-stack-arg-probe -DDEBUG_SUBSYSTEMS=$(DEBUG_SUBSYSTEMS) $(OOBE_VAL)
NASMFLAGS := -f $(NASM_FORMAT)

KERNEL_C := \
  src/kernel/main.c src/system/shell/commands.c \
  src/system/shell/network_commands.c src/system/init/main.c \
  src/system/graphics/wm/wm.c src/system/graphics/wm/presentation.c \
  src/system/graphics/widgets.c src/system/graphics/session.c \
  src/system/services/input/input.c src/system/loader/app_loader.c \
  src/kernel/idt.c src/kernel/paging.c src/kernel/arch_i386.c src/kernel/syscall.c src/system/init/splash/startscreen.c \
  src/system/init/splash/logo_data.c src/system/graphics/screenshot/screenshot.c \
  src/system/graphics/image/png.c src/kernel/drivers/display.c \
  src/kernel/drivers/ata.c src/kernel/drivers/net.c \
  src/kernel/drivers/net_e1000.c src/kernel/drivers/net_tg3.c \
  src/kernel/drivers/serial.c src/kernel/drivers/floppy.c src/kernel/drivers/sb16_driver.c \
  src/kernel/drivers/power.c src/kernel/drivers/acpi.c \
  src/kernel/drivers/ps2.c src/kernel/kernel_net.c \
  src/kernel/net/ipv4.c src/kernel/net/etherframe.c src/kernel/net/arp.c \
  src/kernel/net/tcp.c src/kernel/net/udp.c src/kernel/net/icmp.c \
  src/kernel/net/dhcp.c src/kernel/net/dns.c src/kernel/fs.c \
  src/system/graphics/wm/cursor.c src/kernel/drivers/display/font.c \
  src/kernel/keyboard.c src/system/graphics/fonts/univers85ebo_16.c \
  src/system/graphics/wm/desktop.c src/kernel/task.c \
  src/system/graphics/dialog/dialog.c src/apps/notepad/notepad.c \
  src/apps/viewer/viewer.c src/system/graphics/terminal/terminal.c

KERNEL_ASM := src/kernel/kernel_entry.asm src/kernel/idt_asm.asm \
              src/kernel/task_switch.asm src/kernel/gdt_asm.asm
KERNEL_OBJ := $(KERNEL_ASM:.asm=.o) $(KERNEL_C:.c=.o)
APPS := files mines calc hexview floppy_debug ping netdiag http_test curl \
        crash_debug shutdown userhello sound oobe sysdiag
# Temporary Fastfetch image inclusion for testing.
APP_BINS := $(addsuffix .bin,$(APPS)) browser.bin fastfetch.bin
.PHONY: all run clean clean-build
all: os.img

.PHONY: fastfetch.bin
fastfetch.bin:
	$(MAKE) -C src/apps/fastfetch
	$(PYTHON) -c "from pathlib import Path; Path('fastfetch.bin').write_bytes(Path('src/apps/fastfetch/fastfetch.bin').read_bytes())"

src/boot.bin: Makefile src/boot/boot.asm
	$(NASM) -f bin src/boot/boot.asm -o $@

%.o: %.c Makefile
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.asm Makefile
	$(NASM) $(NASMFLAGS) $< -o $@

$(APPS:%=%.bin): %.bin: Makefile src/apps/%/entry.o src/apps/%/main.o
	$(LD) $(LDFLAGS) -Ttext 0xC00000 -e _start $(filter %.o,$^) -o src/apps/$*/$*.tmp
	$(OBJCOPY) -O binary --remove-section=.bss --remove-section=.reloc src/apps/$*/$*.tmp $@
	$(PYTHON) -c "from pathlib import Path; Path('src/apps/$*/$*.tmp').unlink(missing_ok=True); Path('src/apps/$*/entry.o').unlink(missing_ok=True); Path('src/apps/$*/main.o').unlink(missing_ok=True)"

browser.bin: Makefile src/apps/browser/entry.o src/apps/browser/main.o
	$(LD) $(LDFLAGS) --omagic -Ttext 0xC00000 -e _start $(filter %.o,$^) -o src/apps/browser/browser.tmp
	$(OBJCOPY) -O binary --remove-section=.bss --remove-section=.reloc src/apps/browser/browser.tmp $@
	$(PYTHON) -c "from pathlib import Path; [Path(p).unlink(missing_ok=True) for p in ('src/apps/browser/browser.tmp','src/apps/browser/entry.o','src/apps/browser/main.o')]"

src/kernel/kernel.bin: Makefile kernel.ld $(KERNEL_OBJ)
	$(LD) $(LDFLAGS) -T kernel.ld $(filter %.o,$^) -o src/kernel/kernel.tmp
	$(OBJCOPY) -O binary --remove-section=.bss --remove-section=.reloc src/kernel/kernel.tmp $@
	$(PYTHON) -c "from pathlib import Path; Path('src/kernel/kernel.tmp').unlink(missing_ok=True)"

src/boot_stage2.bin: Makefile src/boot/boot_stage2.asm src/kernel/kernel.bin
	$(PYTHON) -c "import pathlib,subprocess,sys; n=pathlib.Path('src/kernel/kernel.bin').stat().st_size; sectors=(n+511)//512; print(f'Kernel image: {n} bytes ({sectors} sectors)'); sys.exit('Kernel image exceeds the 495-sector boot limit' if not 0 < sectors <= 495 else 0)"
	$(PYTHON) -c "import pathlib,subprocess,sys; n=pathlib.Path('src/kernel/kernel.bin').stat().st_size; sys.exit(subprocess.call(['$(NASM)','-f','bin','-D',f'KERNEL_SECTORS={(n+511)//512}','-D',f'KERNEL_SIZE_BYTES={n}','src/boot/boot_stage2.asm','-o','src/boot_stage2.bin']))"

RESOURCE_WAVS := $(shell $(PYTHON) -c "from pathlib import Path; print(' '.join(str(p) for p in Path('resources').rglob('*') if p.is_file() and p.suffix.lower() == '.wav'))")

os.img: $(RESOURCE_WAVS) resources Makefile src/boot.bin src/boot_stage2.bin src/kernel/kernel.bin $(APP_BINS) mkfat32.py
	$(PYTHON) mkfat32.py os.img
	$(PYTHON) -c "from pathlib import Path; p=Path('os.img'); d=bytearray(p.read_bytes()); b=Path('src/boot.bin').read_bytes(); d[:3]=b[:3]; d[90:512]=b[90:512]; p.write_bytes(d)"
	$(MAKE) clean-build

run: os.img
	$(QEMU) -m 32M -drive file=os.img,format=raw,index=0,media=disk -netdev user,id=net0,net=10.0.1.0/24,dhcpstart=10.0.1.227 -device e1000,netdev=net0 -device sb16 -serial stdio

clean-build:
	$(PYTHON) -c "from pathlib import Path; pats=('*.o','*.tmp'); [p.unlink() for pat in pats for p in Path('.').rglob(pat) if p.is_file()]; [p.unlink(missing_ok=True) for p in [Path('src/boot.bin'),Path('src/boot_stage2.bin'),Path('src/kernel/kernel.bin')]+[Path(f'{a}.bin') for a in 'files mines calc hexview floppy_debug ping netdiag http_test curl crash_debug shutdown userhello sound browser oobe sysdiag'.split()]]"

clean: clean-build
	$(PYTHON) -c "from pathlib import Path; Path('os.img').unlink(missing_ok=True)"
