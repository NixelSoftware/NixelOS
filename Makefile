.DEFAULT_GOAL := all

PYTHON ?= python3
NASM ?= nasm
CC ?= gcc
LD ?= ld
OBJCOPY ?= objcopy
QEMU ?= qemu-system-i386

# Customizable QEMU settings
QEMU_ACCEL ?= auto
QEMU_NETCARD ?= e1000
QEMU_MEMORY ?= 128M
QEMU_DHCP ?= 192.168.100.0/24,dhcpstart=192.168.100.227
QEMU_DISPLAY ?= sdl
QEMU_USB ?= -device piix3-usb-uhci,id=uhci -device usb-mouse,bus=uhci.0
QEMU_AUDIO_DRIVER ?= sdl
QEMU_AUDIO_FLAGS = -audiodev $(QEMU_AUDIO_DRIVER),id=audio0 -machine pcspk-audiodev=audio0
QEMU_EXTRA ?=
QEMU_OPL ?= 1
ifeq ($(QEMU_OPL),1)
QEMU_OPL_FLAGS = -device adlib,audiodev=audio0
else
QEMU_OPL_FLAGS =
endif

ENABLE_OOBE ?= 0
DEBUG_SUBSYSTEMS ?= 0
# Suppress audio HAL, driver, and audio syscall debug logs.
NO_AUDIO_DEBUG ?= 0
STARTUP_TRACE ?= 0
# 0=normal, 1=input-only runtime, 2=normal without audio, 3=idle runtime.
STARTUP_PROFILE ?= 0

ifeq ($(OS),Windows_NT)
  PYTHON := python
  CC := i686-elf-gcc
  LD := i686-elf-ld
  OBJCOPY := i686-elf-objcopy
endif

ifeq ($(QEMU_ACCEL),auto)
  QEMU_KVM_AVAILABLE := $(shell $(PYTHON) -c "import os,platform; print(int(platform.system() == 'Linux' and platform.machine().lower() in ('x86_64', 'amd64', 'i386', 'i686') and os.access('/dev/kvm', os.R_OK | os.W_OK)))")
  ifeq ($(QEMU_KVM_AVAILABLE),1)
    QEMU_ACCEL_FLAGS := -accel kvm -accel tcg
  else
    QEMU_ACCEL_FLAGS := -accel tcg
  endif
else
  QEMU_ACCEL_FLAGS := -accel $(QEMU_ACCEL)
endif

NASM_FORMAT := elf32
LD_FORMAT := elf_i386
LDFLAGS := -m $(LD_FORMAT)

ifneq ($(filter-out 0 1,$(DEBUG_SUBSYSTEMS)),)
  $(error DEBUG_SUBSYSTEMS must be 0 or 1)
endif
ifneq ($(filter-out 0 1,$(NO_AUDIO_DEBUG)),)
  $(error NO_AUDIO_DEBUG must be 0 or 1)
endif
ifneq ($(filter-out 0 1,$(STARTUP_TRACE)),)
  $(error STARTUP_TRACE must be 0 or 1)
endif
ifneq ($(filter-out 0 1 2 3,$(STARTUP_PROFILE)),)
  $(error STARTUP_PROFILE must be 0, 1, 2 or 3)
endif
ifneq ($(filter-out 0 1,$(ENABLE_OOBE)),)
  $(error ENABLE_OOBE must be 0 or 1)
endif

ifeq ($(ENABLE_OOBE),1)
  OOBE_VAL := -DENABLE_OOBE=1
else
  OOBE_VAL :=
endif

CFLAGS := -DSTARTUP_TRACE=$(STARTUP_TRACE) -DSTARTUP_PROFILE=$(STARTUP_PROFILE) \
          -O2 -m32 -ffreestanding -fno-leading-underscore -fno-pie \
          -mno-stack-arg-probe -mno-sse -mno-sse2 -mno-mmx \
          -DDEBUG_SUBSYSTEMS=$(DEBUG_SUBSYSTEMS) -DNO_AUDIO_DEBUG=$(NO_AUDIO_DEBUG) $(OOBE_VAL) -Isrc/include
NASMFLAGS := -f $(NASM_FORMAT) -DSTARTUP_TRACE=$(STARTUP_TRACE)

KERNEL_C := \
  src/kernel/users.c src/kernel/startup_trace.c src/kernel/main.c src/system/shell/commands.c \
  src/system/shell/network_commands.c src/system/init/main.c \
  src/system/graphics/wm/wm.c src/system/graphics/wm/presentation.c \
  src/system/graphics/widgets.c src/system/graphics/session.c \
  src/system/services/input/input.c src/system/services/audio/system_sound.c src/system/loader/app_loader.c \
  src/kernel/idt.c src/kernel/paging.c src/kernel/arch_i386.c src/kernel/arch_fpu.c src/kernel/syscall.c src/system/init/splash/startscreen.c \
  src/system/init/splash/logo_data.c src/system/graphics/screenshot/screenshot.c \
  src/system/graphics/image/png.c src/kernel/drivers/display.c \
  src/kernel/drivers/pci.c src/kernel/drivers/delay.c src/kernel/net/phys/broadcom.c \
  src/kernel/drivers/ata.c src/kernel/net/amd/pcnet.c \
  src/kernel/net/intel/e1000.c src/kernel/net/broadcom/tg3.c src/kernel/net/broadcom/b44.c \
  src/kernel/drivers/serial.c src/kernel/drivers/floppy.c src/kernel/drivers/sb16_driver.c src/kernel/drivers/hda.c src/kernel/drivers/pc_speaker.c \
  src/kernel/drivers/power.c src/kernel/drivers/acpi.c \
  src/kernel/drivers/ps2.c src/kernel/hal/net.c \
  src/kernel/net/ipv4.c src/kernel/net/etherframe.c src/kernel/net/arp.c \
  src/kernel/net/tcp.c src/kernel/net/udp.c src/kernel/net/icmp.c \
  src/kernel/net/dhcp.c src/kernel/net/dns.c src/kernel/fs.c \
  src/system/graphics/wm/cursor.c src/kernel/drivers/display/font.c \
  src/kernel/keyboard.c src/system/graphics/fonts/univers85ebo_16.c \
  src/system/graphics/wm/desktop.c src/kernel/task.c src/kernel/scheduler.c \
  src/kernel/sched_policy.c src/kernel/sched_wide.c src/kernel/mutex.c \
  src/system/graphics/dialog/dialog.c src/apps/notepad/notepad.c \
  src/apps/viewer/viewer.c src/system/graphics/terminal/terminal.c \
  src/kernel/drivers/rtc.c src/system/graphics/wm/desktop_wallpaper.c \
  src/system/icons/network.h src/kernel/hal/audio.c \
  src/system/icons/sound.h src/system/graphics/wm/start.c \
  src/kernel/hal/usb.c src/kernel/drivers/usb/uhci.c \
  src/kernel/login.c

KERNEL_ASM := src/kernel/kernel_entry.asm src/kernel/idt_asm.asm \
              src/kernel/task_switch.asm src/kernel/gdt_asm.asm
KERNEL_OBJ := $(KERNEL_ASM:.asm=.o) $(KERNEL_C:.c=.o)
KERNEL_HEADERS := src/kernel/startup_trace.h src/kernel/task.h src/kernel/scheduler.h \
                  src/kernel/mutex.h src/kernel/sched_policy.h src/kernel/sched_wide.h \
                  src/kernel/arch_fpu.h
$(KERNEL_OBJ): $(KERNEL_HEADERS)

.PHONY: FORCE_AUDIO_DEBUG_CONFIG
.audio-debug-config: FORCE_AUDIO_DEBUG_CONFIG
	$(PYTHON) -c "from pathlib import Path; p=Path('$@'); value='$(NO_AUDIO_DEBUG)\n'; p.write_text(value) if not p.exists() or p.read_text() != value else None"

src/kernel/hal/audio.o src/kernel/drivers/hda.o src/kernel/drivers/sb16_driver.o src/kernel/syscall.o: .audio-debug-config src/kernel/drivers/serial.h

STANDARD_APPS := files mines chess calc hexview floppy_debug ping netdiag com0 http_test curl \
                 crash_debug shutdown userhello sound oobe sysdiag reboot setwall shutdlg
APPS := $(STANDARD_APPS) syndig
APP_BINS := $(addsuffix .bin,$(APPS)) browser.bin fastfetch.bin

.PHONY: all run clean clean-build FORCE_VERSION

src/include/os_version.h: FORCE_VERSION VERSION tools/generate_version.py
	$(PYTHON) tools/generate_version.py

src/system/shell/commands.o src/system/graphics/wm/start.o: src/include/os_version.h
all: os.img

%.o: %.c Makefile
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.asm Makefile
	$(NASM) $(NASMFLAGS) $< -o $@

$(STANDARD_APPS:%=%.bin): %.bin: Makefile src/apps/%/entry.o src/apps/%/main.o
	$(LD) $(LDFLAGS) -Ttext 0xC00000 -e _start $(filter %.o,$^) -o src/apps/$*/$*.tmp
	$(OBJCOPY) -O binary --remove-section=.bss --remove-section=.reloc src/apps/$*/$*.tmp $@
	$(PYTHON) -c "from pathlib import Path; Path('src/apps/$*/$*.tmp').unlink(missing_ok=True)"

src/apps/chess/piece_assets.h: src/apps/chess/embed_assets.py $(wildcard src/apps/chess/assets/*.png)
	$(PYTHON) src/apps/chess/embed_assets.py $@
src/apps/chess/main.o: src/apps/chess/piece_assets.h
src/apps/chess/main.o src/apps/chess/engine.o: src/apps/chess/engine.h
chess.bin: src/apps/chess/engine.o

src/apps/sound/mp3.o: CFLAGS += -Isrc/apps/sound/compat
src/apps/sound/mp3.o: src/apps/sound/mp3.h src/apps/sound/upstream/minimp3.h src/apps/sound/compat/string.h src/apps/sound/compat/stdlib.h
src/apps/sound/main.o: src/apps/sound/mp3.h
sound.bin: src/apps/sound/mp3.o

SYNDIG_DIR := src/apps/syndig
SYNDIG_C := \
    $(SYNDIG_DIR)/main.c \
    $(SYNDIG_DIR)/midi_file.c \
    $(SYNDIG_DIR)/instruments.c \
    $(SYNDIG_DIR)/opl.c \
    $(SYNDIG_DIR)/compat/runtime.c \
    $(SYNDIG_DIR)/compat/div64.c \
    $(SYNDIG_DIR)/upstream/buffer.c \
    $(SYNDIG_DIR)/upstream/channel.c \
    $(SYNDIG_DIR)/upstream/compressor.c \
    $(SYNDIG_DIR)/upstream/envelope.c \
    $(SYNDIG_DIR)/upstream/hertz.c \
    $(SYNDIG_DIR)/upstream/lane.c \
    $(SYNDIG_DIR)/upstream/midi.c \
    $(SYNDIG_DIR)/upstream/oscillator.c \
    $(SYNDIG_DIR)/upstream/polyphony.c
SYNDIG_OBJ := $(SYNDIG_DIR)/entry.o $(SYNDIG_C:.c=.o)
SYNDIG_HEADERS := $(wildcard $(SYNDIG_DIR)/*.h $(SYNDIG_DIR)/compat/*.h $(SYNDIG_DIR)/upstream/*.h)
$(SYNDIG_C:.c=.o): $(SYNDIG_HEADERS)

syndig.bin: Makefile $(SYNDIG_OBJ)
	$(LD) $(LDFLAGS) --omagic -Ttext 0xC00000 -e _start $(filter %.o,$^) -o $(SYNDIG_DIR)/syndig.tmp
	$(OBJCOPY) -O binary --remove-section=.bss --remove-section=.reloc $(SYNDIG_DIR)/syndig.tmp $@
	$(PYTHON) -c "from pathlib import Path; Path('$(SYNDIG_DIR)/syndig.tmp').unlink(missing_ok=True)"

NIXPLORER_DIR := src/apps/nixplorer
BROWSER_C := $(NIXPLORER_DIR)/browser/main.c \
             $(NIXPLORER_DIR)/engine/net/http.c \
             $(NIXPLORER_DIR)/engine/net/response.c \
             $(NIXPLORER_DIR)/engine/platform/tcp.c \
             $(NIXPLORER_DIR)/engine/parser/html.c \
             $(NIXPLORER_DIR)/engine/font/font.c \
             $(NIXPLORER_DIR)/engine/dom/document.c \
             $(NIXPLORER_DIR)/engine/style/style.c \
             $(NIXPLORER_DIR)/engine/style/selectors.c \
             $(NIXPLORER_DIR)/engine/layout/box.c \
             $(NIXPLORER_DIR)/engine/layout/flex.c \
             $(NIXPLORER_DIR)/engine/layout/grid.c \
             $(NIXPLORER_DIR)/engine/parser/css_grid.c \
             $(NIXPLORER_DIR)/engine/parser/css_color.c
BROWSER_OBJ := $(NIXPLORER_DIR)/browser/entry.o $(BROWSER_C:.c=.o)
BROWSER_HEADERS := $(wildcard $(NIXPLORER_DIR)/engine/*/*.h)
$(BROWSER_C:.c=.o): $(BROWSER_HEADERS)
$(NIXPLORER_DIR)/engine/parser/html.o: $(NIXPLORER_DIR)/engine/parser/entities.inc
$(NIXPLORER_DIR)/engine/parser/css_color.o: $(NIXPLORER_DIR)/engine/parser/colors.inc
src/system/graphics/image/png.o: $(NIXPLORER_DIR)/engine/paint/png.c $(NIXPLORER_DIR)/engine/paint/png.h

browser.bin: Makefile $(BROWSER_OBJ)
	$(LD) $(LDFLAGS) --omagic -Ttext 0xC00000 -e _start $(filter %.o,$^) -o $(NIXPLORER_DIR)/browser/browser.tmp
	$(OBJCOPY) -O binary --remove-section=.bss --remove-section=.reloc $(NIXPLORER_DIR)/browser/browser.tmp $@
	$(PYTHON) -c "from pathlib import Path; Path('$(NIXPLORER_DIR)/browser/browser.tmp').unlink(missing_ok=True)"

.PHONY: fastfetch.bin
fastfetch.bin: src/include/os_version.h
	$(MAKE) -C src/apps/fastfetch
	$(PYTHON) -c "from pathlib import Path; Path('fastfetch.bin').write_bytes(Path('src/apps/fastfetch/fastfetch.bin').read_bytes())"

src/boot.bin: Makefile src/boot/boot.asm
	$(NASM) -f bin src/boot/boot.asm -o $@

src/kernel/kernel.bin: Makefile kernel.ld $(KERNEL_OBJ)
	$(LD) $(LDFLAGS) -T kernel.ld $(filter %.o,$^) -o src/kernel/kernel.tmp
	$(OBJCOPY) -O binary --remove-section=.bss --remove-section=.reloc src/kernel/kernel.tmp $@
ifeq ($(STARTUP_TRACE),1)
	$(PYTHON) -c "from pathlib import Path; Path('src/kernel/kernel.trace.elf').write_bytes(Path('src/kernel/kernel.tmp').read_bytes())"
endif
	$(PYTHON) -c "from pathlib import Path; Path('src/kernel/kernel.tmp').unlink(missing_ok=True)"

src/boot_stage2.bin: Makefile src/boot/boot_stage2.asm src/kernel/kernel.bin
	$(PYTHON) -c "import pathlib,sys; n=pathlib.Path('src/kernel/kernel.bin').stat().st_size; sectors=(n+511)//512; print(f'Kernel image: {n} bytes ({sectors} sectors)'); sys.exit('Kernel image exceeds the 495-sector boot limit' if not 0 < sectors <= 495 else 0)"
	$(PYTHON) -c "import pathlib,subprocess,sys; n=pathlib.Path('src/kernel/kernel.bin').stat().st_size; sys.exit(subprocess.call(['$(NASM)','-f','bin','-D',f'KERNEL_SECTORS={(n+511)//512}','-D',f'KERNEL_SIZE_BYTES={n}','src/boot/boot_stage2.asm','-o','src/boot_stage2.bin']))"

RESOURCE_AUDIO := $(shell $(PYTHON) -c "from pathlib import Path; print(' '.join(str(p) for p in Path('resources').rglob('*') if p.is_file() and p.suffix.lower() in {'.mid', '.midi', '.wav', '.mp3'}))")

os.img: $(RESOURCE_AUDIO) resources Makefile src/boot.bin src/boot_stage2.bin src/kernel/kernel.bin $(APP_BINS) mkfat32.py
	$(PYTHON) mkfat32.py os.img.new
	$(PYTHON) -c "from pathlib import Path; p=Path('os.img.new'); d=bytearray(p.read_bytes()); b=Path('src/boot.bin').read_bytes(); d[:3]=b[:3]; d[90:512]=b[90:512]; p.write_bytes(d); p.replace('os.img')"
	$(MAKE) clean-build

run: os.img
	$(QEMU) $(QEMU_ACCEL_FLAGS) -smp 1 -m $(QEMU_MEMORY) -drive file=os.img,format=raw,index=0,media=disk -netdev user,id=net0,net=$(QEMU_DHCP) -device $(QEMU_NETCARD),netdev=net0 -display $(QEMU_DISPLAY) $(QEMU_AUDIO_FLAGS) -device sb16,audiodev=audio0 $(QEMU_OPL_FLAGS) -serial stdio -rtc base=localtime $(QEMU_USB) $(QEMU_EXTRA)

clean-build:
	$(PYTHON) -c "from pathlib import Path; pats=('*.o','*.tmp'); [p.unlink() for pat in pats for p in Path('.').rglob(pat) if p.is_file()]; [p.unlink(missing_ok=True) for p in [Path('src/boot.bin'),Path('src/boot_stage2.bin'),Path('src/kernel/kernel.bin')]+[Path(f'{a}.bin') for a in '$(STANDARD_APPS) syndig browser fastfetch'.split()]]"

clean: clean-build
	$(PYTHON) -c "from pathlib import Path; Path('os.img').unlink(missing_ok=True)"

src/kernel/net/broadcom/b44.o: src/kernel/net/broadcom/b44.h src/kernel/net/broadcom/b44_regs.h src/kernel/hal/net_types.h
src/kernel/hal/net.o: src/kernel/net/broadcom/b44.h

src/kernel/drivers/hda.o: src/kernel/drivers/hda.h src/kernel/hal/audio.h
src/kernel/hal/audio.o: src/kernel/drivers/hda.h

$(KERNEL_C:.c=.o): src/kernel/drivers/pci.h src/kernel/drivers/delay.h
src/kernel/net/broadcom/tg3.o: src/kernel/net/broadcom/tg3_regs.h

src/kernel/net/broadcom/tg3.o src/kernel/net/phys/broadcom.o: src/kernel/net/phys/broadcom.h

$(KERNEL_C:.c=.o): src/kernel/hal/net.h src/kernel/hal/net_types.h
src/kernel/net/amd/pcnet.o: src/kernel/net/amd/pcnet.h
src/kernel/net/intel/e1000.o: src/kernel/net/intel/e1000.h
src/kernel/net/broadcom/tg3.o: src/kernel/net/broadcom/tg3.h
src/kernel/hal/net.o: src/kernel/net/amd/pcnet.h src/kernel/net/intel/e1000.h src/kernel/net/broadcom/tg3.h
